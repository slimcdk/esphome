from collections.abc import Awaitable, Callable
from dataclasses import dataclass, field
import logging
from typing import Any

from esphome import automation
import esphome.codegen as cg
from esphome.components.canbus import CONF_CANBUS_ID, CanbusComponent
from esphome.components.const import CONF_VALUE_TYPE
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_DEVICE,
    CONF_DEVICES,
    CONF_ID,
    CONF_PLATFORM,
    CONF_TIMEOUT,
    CONF_UPDATE_INTERVAL,
    SCHEDULER_DONT_RUN,
)
from esphome.core import CORE, ID
from esphome.cpp_generator import MockObj
import esphome.final_validate as fv
from esphome.types import ConfigType, TemplateArgsType

_LOGGER = logging.getLogger(__name__)

DOMAIN = "masterbus"
CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["canbus"]
MULTI_CONF = True

CONF_LOG_ALL_FRAMES = "log_all_frames"
CONF_MASTERBUS_DEVICE_ID = "masterbus_device_id"
CONF_NODE_REQUEST_INTERVAL = "node_request_interval"
CONF_ON_OFFLINE = "on_offline"
CONF_ON_ONLINE = "on_online"
CONF_ON_UNKNOWN_FRAME = "on_unknown_frame"
CONF_PARAM = "param"
CONF_TAB = "tab"

# Both bounds mirror masterbus_protocol.h, which is where the wire format is recorded.
MAX_DEVICE_ADDRESS = 0x007FFFFF
MAX_PARAM = 0xFFFF
# The vendor library's own address, which a hub borrows unless given one of its own.
HUB_ADDRESS = 0x500001
DEVICE_CLASS_SHIFT = 18

# How many devices a scan will list. A MasterBus network is a boat's electrical system, not a
# fieldbus - the largest installation seen carries nine devices.
MAX_SCAN_DEVICES = 32

masterbus_ns = cg.esphome_ns.namespace("masterbus")
MasterbusHub = masterbus_ns.class_("MasterbusHub", cg.Component)
MasterbusDevice = masterbus_ns.class_("MasterbusDevice")
MasterbusEntity = masterbus_ns.class_("MasterbusEntity", cg.PollingComponent)

MasterbusTab = masterbus_ns.enum("MasterbusTab", is_class=True)
MasterbusValueType = masterbus_ns.enum("MasterbusValueType", is_class=True)

# Bootloader is deliberately absent: firmware update over MasterBus is out of scope.
TABS = {
    "monitoring": MasterbusTab.MASTERBUS_TAB_MONITORING,
    "alarm": MasterbusTab.MASTERBUS_TAB_ALARM,
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

# Every hub declares its lists at the same size, so each slot is requested under the hub that owns
# it and the size is the largest hub's count rather than the total.
_request_device_slot = cg.slot_counter("MASTERBUS_DEVICE_COUNT")
_request_entity_slot = cg.slot_counter("MASTERBUS_ENTITY_COUNT")
_request_unknown_frame_slot = cg.slot_counter("MASTERBUS_UNKNOWN_FRAME_COUNT")
_request_info_slot = cg.slot_counter("MASTERBUS_INFO_COUNT")


@dataclass
class MasterbusData:
    # The hub each declared device belongs to, by id, so that an entity, which names only its
    # device, takes its slot in the right hub's list.
    device_hubs: dict[str, str] = field(default_factory=dict)


def _get_data() -> MasterbusData:
    if DOMAIN not in CORE.data:
        CORE.data[DOMAIN] = MasterbusData()
    return CORE.data[DOMAIN]


def _address_in_range(key: str, value: Any) -> int:
    address = cv.hex_int(value)
    if not 0 <= address <= MAX_DEVICE_ADDRESS:
        raise cv.Invalid(
            f"'{key}' must be a MasterBus address between 0x000000 and "
            f"0x{MAX_DEVICE_ADDRESS:06X}, but 0x{address:06X} was given. The extended CAN "
            f"identifier gives the address 23 bits; the rest of it carries the message type."
        )
    return address


def _device_address(value: Any) -> int:
    return _address_in_range(CONF_DEVICE, value)


def _hub_address(value: Any) -> int:
    address = _address_in_range(CONF_ADDRESS, value)
    if address >> DEVICE_CLASS_SHIFT == 0:
        raise cv.Invalid(
            f"'{CONF_ADDRESS}' 0x{address:06X} has no device class: the top five bits of a "
            f"MasterBus address say what kind of node it is, and no node is class 0"
        )
    return address


def _validate_unique_devices(config: ConfigType) -> ConfigType:
    if (address := config[CONF_ADDRESS]) in {
        device[CONF_DEVICE] for device in config.get(CONF_DEVICES, [])
    }:
        raise cv.Invalid(
            f"the hub's own '{CONF_ADDRESS}' 0x{address:06X} is also a declared device. Every device "
            f"answers a node request except the one whose address it carries, so that device would "
            f"never be heard announcing itself."
        )
    declared: dict[int, str] = {}
    for device in config.get(CONF_DEVICES, []):
        address = device[CONF_DEVICE]
        if (first := declared.get(address)) is not None:
            raise cv.Invalid(
                f"MasterBus device 0x{address:06X} is declared twice, as '{first}' and as "
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
            cv.Optional(CONF_ADDRESS, default=HUB_ADDRESS): _hub_address,
            cv.Optional(CONF_LOG_ALL_FRAMES, default=False): cv.boolean,
            # Devices announce themselves only when asked. Where no other node asks, a hub can.
            # Every device answers every request, three times over, so it is kept above a second.
            cv.Optional(CONF_NODE_REQUEST_INTERVAL): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=1)),
            ),
            cv.Optional(CONF_ON_UNKNOWN_FRAME): automation.validate_automation(),
            cv.Optional(CONF_DEVICES): cv.ensure_list(DEVICE_SCHEMA),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_unique_devices,
)


def _validate_poll_cadence(config: ConfigType) -> ConfigType:
    """Warn about a device given less silence than its own fastest poller leaves it.

    A device says nothing unless it is asked. Where this hub holds the only node asking, the
    frames that keep a device online are the answers to its own entities' polls and to its own
    node requests, so a device whose timeout is no longer than the interval between them goes
    offline between answers. Another node on the bus may well be asking too, which is why this
    is a warning.
    """
    node_requests = config.get(CONF_NODE_REQUEST_INTERVAL)
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
        # The device announces itself in answer to every node request this hub sends.
        if node_requests is not None and node_requests.total_milliseconds < timeout:
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


def _validate_unique_hub_address(config: ConfigType) -> ConfigType:
    """Refuse a second hub on the same bus under the same address.

    Both would transmit under one identity, and each would take the other's answers as its own.
    """
    for other in fv.full_config.get().get(DOMAIN, []):
        if (
            str(other[CONF_ID]) != str(config[CONF_ID])
            and str(other[CONF_CANBUS_ID]) == str(config[CONF_CANBUS_ID])
            and other[CONF_ADDRESS] == config[CONF_ADDRESS]
        ):
            raise cv.Invalid(
                f"hubs '{config[CONF_ID]}' and '{other[CONF_ID]}' both speak as "
                f"0x{config[CONF_ADDRESS]:06X} on '{config[CONF_CANBUS_ID]}'. Give one of them an "
                f"'{CONF_ADDRESS}' of its own: two nodes under one address take each other's "
                f"answers as their own."
            )
    return config


def _final_validate(config: ConfigType) -> ConfigType:
    _validate_unique_hub_address(config)
    return _validate_poll_cadence(config)


FINAL_VALIDATE_SCHEMA = _final_validate


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
            cv.Optional(CONF_TAB, default="monitoring"): cv.enum(TABS, lower=True),
            # A device answers a field only when asked. Left unset a PollingComponent never runs,
            # so the entity stays silent - which is what a first look at a live bus should do.
            cv.Optional(CONF_UPDATE_INTERVAL): cv.update_interval,
            cv.Optional(CONF_VALUE_TYPE, default=default_value_type): cv.enum(
                {name: VALUE_TYPES[name] for name in value_types}, lower=True
            ),
            # Zero would read as "follow the device", which is what leaving the key out already
            # means. Refused rather than given a second spelling.
            cv.Optional(CONF_TIMEOUT): cv.All(
                cv.positive_not_null_time_period, cv.positive_time_period_milliseconds
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA)


async def new_entity(
    creator: Callable[..., Awaitable[MockObj]], config: ConfigType, **kwargs: Any
) -> MockObj:
    """Build one MasterBus entity and attach it to its device.

    `creator` is the platform's own `new_*` helper, which is handed the address every MasterBus
    entity carries; `kwargs` are whatever that platform needs on top.
    """
    device_id = config[CONF_MASTERBUS_DEVICE_ID]
    device = await cg.get_variable(device_id)
    var = await creator(
        config,
        device,
        config[CONF_PARAM],
        config[CONF_TAB],
        config[CONF_VALUE_TYPE],
        **kwargs,
    )
    _request_entity_slot(_get_data().device_hubs[str(device_id)])
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


async def register_info_entity(device_id: ID, device: MockObj, var: MockObj) -> None:
    """Hand the hub an entity that shows what a device says about itself, read off the bus."""
    _request_info_slot(_get_data().device_hubs[str(device_id)])
    cg.add(device.register_info_entity(var))


SCAN_ACTION_SCHEMA = cv.Schema({cv.GenerateID(): cv.use_id(MasterbusHub)})
_scan_action = automation.apply_action_builder(
    "masterbus.scan", SCAN_ACTION_SCHEMA, automation.ApplyCall("start_scan()")
)


@automation.register_action(
    "masterbus.scan", automation.ApplyAction, SCAN_ACTION_SCHEMA, synchronous=True
)
async def scan_action_to_code(
    config: ConfigType,
    action_id: ID,
    template_arg: cg.TemplateArguments,
    args: TemplateArgsType,
) -> MockObj:
    # The scan is compiled in only where something starts one, at boot or from a button.
    cg.add_define("USE_MASTERBUS_SCAN")
    cg.add_define("MASTERBUS_SCAN_MAX_DEVICES", MAX_SCAN_DEVICES)
    return await _scan_action(config, action_id, template_arg, args)


async def to_code(config: ConfigType) -> None:
    canbus = await cg.get_variable(config[CONF_CANBUS_ID])
    var = cg.new_Pvariable(config[CONF_ID], canbus)
    await cg.register_component(var, config)
    if config[CONF_ADDRESS] != HUB_ADDRESS:
        cg.add(var.set_address(config[CONF_ADDRESS]))

    if config[CONF_LOG_ALL_FRAMES]:
        cg.add_define("USE_MASTERBUS_LOG_ALL_FRAMES")
    if (interval := config.get(CONF_NODE_REQUEST_INTERVAL)) is not None:
        cg.add_define("USE_MASTERBUS_NODE_REQUEST")
        cg.add(var.set_node_request_interval(interval))

    for conf in config.get(CONF_ON_UNKNOWN_FRAME, []):
        # The frames this fires for are by definition ones nothing else reads, so the cost of
        # recognising them is only paid where somebody asked for it.
        _request_unknown_frame_slot(str(config[CONF_ID]))
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
        _request_device_slot(str(config[CONF_ID]))
        _get_data().device_hubs[str(device_config[CONF_ID])] = str(config[CONF_ID])
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
