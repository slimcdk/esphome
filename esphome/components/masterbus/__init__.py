from collections.abc import Awaitable, Callable
import logging
from typing import Any

from esphome import automation
import esphome.codegen as cg
from esphome.components.canbus import CONF_CANBUS_ID, CanbusComponent
from esphome.components.const import CONF_VALUE_TYPE
import esphome.config_validation as cv
from esphome.const import (
    CONF_DEVICE,
    CONF_DEVICES,
    CONF_ID,
    CONF_PLATFORM,
    CONF_SCAN,
    CONF_TIMEOUT,
    CONF_UPDATE_INTERVAL,
    SCHEDULER_DONT_RUN,
)
from esphome.cpp_generator import MockObj
import esphome.final_validate as fv
from esphome.types import ConfigType

_LOGGER = logging.getLogger(__name__)

DOMAIN = "masterbus"
CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["canbus"]
MULTI_CONF = True

CONF_LOG_ALL_FRAMES = "log_all_frames"
CONF_MASTERBUS_DEVICE_ID = "masterbus_device_id"
CONF_ON_OFFLINE = "on_offline"
CONF_ON_ONLINE = "on_online"
CONF_ON_UNKNOWN_FRAME = "on_unknown_frame"
CONF_PARAM = "param"
CONF_TAB = "tab"

# Both bounds mirror masterbus_protocol.h, which is where the wire format is recorded.
MAX_DEVICE_ADDRESS = 0x007FFFFF
MAX_PARAM = 0xFFFF

# How many devices a scan will list. A MasterBus network is a boat's electrical system, not a
# fieldbus - the largest installation seen carries nine devices.
MAX_SCAN_DEVICES = 32

masterbus_ns = cg.esphome_ns.namespace("masterbus")
MasterbusHub = masterbus_ns.class_("MasterbusHub", cg.Component)
MasterbusDevice = masterbus_ns.class_("MasterbusDevice")
MasterbusEntity = masterbus_ns.class_("MasterbusEntity", cg.PollingComponent)

MasterbusTab = masterbus_ns.enum("MasterbusTab", is_class=True)
MasterbusValueType = masterbus_ns.enum("MasterbusValueType", is_class=True)

# Bootloader is deliberately absent: firmware update over MasterBus is out of scope. So is alarm,
# which a scan still walks and reports - it is an entity on it that cannot work.
TABS = {
    "monitoring": MasterbusTab.MASTERBUS_TAB_MONITORING,
    "history": MasterbusTab.MASTERBUS_TAB_HISTORY,
    "configuration": MasterbusTab.MASTERBUS_TAB_CONFIGURATION,
}

VALUE_TYPE_TEXT = "text"

VALUE_TYPES = {
    "float": MasterbusValueType.MASTERBUS_VALUE_TYPE_FLOAT,
    "date": MasterbusValueType.MASTERBUS_VALUE_TYPE_DATE,
    "time": MasterbusValueType.MASTERBUS_VALUE_TYPE_TIME,
    "boolean": MasterbusValueType.MASTERBUS_VALUE_TYPE_BOOLEAN,
    "list_option": MasterbusValueType.MASTERBUS_VALUE_TYPE_LIST_OPTION,
    VALUE_TYPE_TEXT: MasterbusValueType.MASTERBUS_VALUE_TYPE_TEXT,
    "device_id": MasterbusValueType.MASTERBUS_VALUE_TYPE_DEVICE_ID,
    "eventable": MasterbusValueType.MASTERBUS_VALUE_TYPE_EVENTABLE,
}

_request_device_slot = cg.slot_counter("MASTERBUS_DEVICE_COUNT")
_request_entity_slot = cg.slot_counter("MASTERBUS_ENTITY_COUNT")
_request_unknown_frame_slot = cg.slot_counter("MASTERBUS_UNKNOWN_FRAME_COUNT")


def _device_address(value: Any) -> int:
    address = cv.hex_int(value)
    if not 0 <= address <= MAX_DEVICE_ADDRESS:
        raise cv.Invalid(
            f"'{CONF_DEVICE}' must be a MasterBus device identifier between 0x0 and "
            f"{MAX_DEVICE_ADDRESS:#X}, but {address:#X} was given. The extended CAN identifier "
            f"gives the address 23 bits; the rest of it carries the message type."
        )
    return address


def _validate_unique_devices(config: ConfigType) -> ConfigType:
    declared: dict[int, str] = {}
    for device in config.get(CONF_DEVICES, []):
        address = device[CONF_DEVICE]
        if (first := declared.get(address)) is not None:
            raise cv.Invalid(
                f"MasterBus device {address:#X} is declared twice, as '{first}' and as "
                f"'{device[CONF_ID]}'. One identifier is one device, so two declarations would "
                f"give two sets of entities reading the same source."
            )
        declared[address] = str(device[CONF_ID])
    return config


DEVICE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MasterbusDevice),
        cv.Required(CONF_DEVICE): _device_address,
        cv.Optional(CONF_TIMEOUT, default="60s"): cv.All(
            cv.positive_not_null_time_period, cv.positive_time_period_milliseconds
        ),
        cv.Optional(CONF_ON_ONLINE): automation.validate_automation(),
        cv.Optional(CONF_ON_OFFLINE): automation.validate_automation(),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MasterbusHub),
            cv.Required(
                CONF_CANBUS_ID,
                msg="a MasterBus hub needs the id of the canbus component it listens on",
            ): cv.use_id(CanbusComponent),
            cv.Optional(CONF_SCAN, default=False): cv.boolean,
            cv.Optional(CONF_LOG_ALL_FRAMES, default=False): cv.boolean,
            cv.Optional(CONF_ON_UNKNOWN_FRAME): automation.validate_automation(),
            cv.Optional(CONF_DEVICES): cv.ensure_list(DEVICE_SCHEMA),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_unique_devices,
)


def _tab(value: Any) -> Any:
    """The tab an entity lives on, of those whose values can be read."""
    if isinstance(value, str) and value.lower() == "alarm":
        raise cv.Invalid(
            "the alarm tab cannot be read: the vendor files the message carrying an alarm's value "
            "with the broadcast messages rather than with the tabs, and that message is not "
            "decoded, so an entity here would never be answered. A scan still walks the tab and "
            "reports what is on it."
        )
    return cv.enum(TABS, lower=True)(value)


def _validate_poll_cadence(config: ConfigType) -> ConfigType:
    """Warn about a device given less silence than its own fastest poller leaves it.

    A device says nothing unless it is asked. Where this hub holds the only node asking, the
    frames that keep a device online are the answers to its own entities' polls, so a device
    whose timeout is no longer than the interval between them goes offline between answers.
    Another node on the bus may well be asking too, which is why this is a warning.
    """
    fastest: dict[str, int] = {}
    for entries in fv.full_config.get().values():
        if not isinstance(entries, list):
            continue
        for entry in entries:
            if not isinstance(entry, dict) or entry.get(CONF_PLATFORM) != DOMAIN:
                continue
            interval = entry.get(CONF_UPDATE_INTERVAL)
            if interval is None or interval.total_milliseconds == SCHEDULER_DONT_RUN:
                continue
            device_id = str(entry[CONF_MASTERBUS_DEVICE_ID])
            fastest[device_id] = min(
                fastest.get(device_id, interval.total_milliseconds),
                interval.total_milliseconds,
            )

    for device in config.get(CONF_DEVICES, []):
        interval = fastest.get(str(device[CONF_ID]))
        timeout = device[CONF_TIMEOUT].total_milliseconds
        if interval is None or interval < timeout:
            continue
        _LOGGER.warning(
            "Device 0x%06X is reported offline after %d ms without a frame, but the fields "
            "configured for it are polled no more often than every %d ms. A device answers only "
            "when it is asked, so unless something else on the bus asks it for something, it will "
            "go offline between answers.",
            device[CONF_DEVICE],
            timeout,
            interval,
        )
    return config


FINAL_VALIDATE_SCHEMA = _validate_poll_cadence


def entity_schema(
    *, value_types: tuple[str, ...], default_value_type: str
) -> cv.Schema:
    """The address every MasterBus entity carries, plus its declared value type.

    A platform passes the value types it can render. The type is declared rather than inferred so
    that a frame carrying something else is dropped instead of misread as a number.
    """
    return cv.Schema(
        {
            cv.GenerateID(CONF_MASTERBUS_DEVICE_ID): cv.use_id(MasterbusDevice),
            cv.Required(CONF_PARAM): cv.hex_int_range(min=0, max=MAX_PARAM),
            cv.Optional(CONF_TAB, default="monitoring"): _tab,
            # A device answers a field only when asked. Left unset a PollingComponent never runs,
            # so the entity stays silent - which is what a first look at a live bus should do.
            cv.Optional(CONF_UPDATE_INTERVAL): cv.update_interval,
            cv.Optional(CONF_VALUE_TYPE, default=default_value_type): cv.enum(
                {name: VALUE_TYPES[name] for name in value_types}, lower=True
            ),
            cv.Optional(CONF_TIMEOUT): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA)


async def new_entity(
    creator: Callable[..., Awaitable[MockObj]], config: ConfigType, **kwargs: Any
) -> MockObj:
    """Build one MasterBus entity and attach it to its device.

    `creator` is the platform's own `new_*` helper, which is handed the address every MasterBus
    entity carries; `kwargs` are whatever that platform needs on top.
    """
    device = await cg.get_variable(config[CONF_MASTERBUS_DEVICE_ID])
    var = await creator(
        config,
        device,
        config[CONF_PARAM],
        config[CONF_TAB],
        config[CONF_VALUE_TYPE],
        **kwargs,
    )
    _request_entity_slot()
    await cg.register_component(var, config)
    if config[CONF_VALUE_TYPE] == VALUE_TYPE_TEXT:
        # A text field answers with a string table entry number, so the hub needs the second read
        # that turns it into text. Nothing else needs it, so nothing else pays for it.
        cg.add_define("USE_MASTERBUS_TEXT")
    if (timeout := config.get(CONF_TIMEOUT)) is not None:
        cg.add(var.set_stale_timeout(timeout))
    # Registration goes through the device rather than the hub so that a platform needs only the
    # one id the user writes; the device knows which hub it belongs to and the hub does not.
    cg.add(device.register_entity(var))
    return var


async def to_code(config: ConfigType) -> None:
    canbus = await cg.get_variable(config[CONF_CANBUS_ID])
    var = cg.new_Pvariable(config[CONF_ID], canbus)
    await cg.register_component(var, config)

    if config[CONF_SCAN]:
        cg.add_define("USE_MASTERBUS_SCAN")
        cg.add_define("MASTERBUS_SCAN_MAX_DEVICES", MAX_SCAN_DEVICES)
    if config[CONF_LOG_ALL_FRAMES]:
        cg.add_define("USE_MASTERBUS_LOG_ALL_FRAMES")

    for conf in config.get(CONF_ON_UNKNOWN_FRAME, []):
        # The frames this fires for are by definition ones nothing else reads, so the cost of
        # recognising them is only paid where somebody asked for it.
        _request_unknown_frame_slot()
        await automation.build_callback_automation(
            var,
            "add_on_unknown_frame_callback",
            [
                (cg.std_vector.template(cg.uint8), "x"),
                (cg.uint8, "type"),
                (cg.uint32, "device"),
            ],
            conf,
        )

    for device_config in config.get(CONF_DEVICES, []):
        _request_device_slot()
        device = cg.new_Pvariable(
            device_config[CONF_ID], var, device_config[CONF_DEVICE]
        )
        cg.add(device.set_timeout(device_config[CONF_TIMEOUT]))
        cg.add(var.register_device(device))
        for conf_key, callback in (
            (CONF_ON_ONLINE, "add_on_online_callback"),
            (CONF_ON_OFFLINE, "add_on_offline_callback"),
        ):
            for conf in device_config.get(conf_key, []):
                await automation.build_callback_automation(device, callback, [], conf)
