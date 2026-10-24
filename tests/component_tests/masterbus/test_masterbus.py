"""Tests for masterbus configuration validation and code generation.

A MasterBus device identifier is a number the user reads off a scan and types by hand, so the
schema has to catch a typo before it reaches a bus that controls battery relays. These tests pin
the rejections and the storage the configuration sizes.
"""

from collections.abc import Callable
from pathlib import Path

import pytest

from esphome import config_validation as cv, yaml_util
from esphome.components.canbus import CONF_CANBUS_ID
from esphome.components.const import CONF_VALUE_TYPE
from esphome.components.masterbus import (
    CONF_LOG_ALL_FRAMES,
    CONF_MASTERBUS_DEVICE_ID,
    CONF_NODE_REQUEST_INTERVAL,
    CONF_PARAM,
    CONF_TAB,
    CONFIG_SCHEMA,
    DEVICE_SCHEMA,
)
from esphome.components.masterbus.binary_sensor import (
    CONFIG_SCHEMA as BINARY_SENSOR_SCHEMA,
)
from esphome.components.masterbus.button import CONFIG_SCHEMA as BUTTON_SCHEMA
from esphome.components.masterbus.number import CONFIG_SCHEMA as NUMBER_SCHEMA
from esphome.components.masterbus.select import CONFIG_SCHEMA as SELECT_SCHEMA
from esphome.components.masterbus.sensor import CONFIG_SCHEMA as SENSOR_SCHEMA
from esphome.components.masterbus.switch import CONFIG_SCHEMA as SWITCH_SCHEMA
from esphome.components.masterbus.text_sensor import CONFIG_SCHEMA as TEXT_SENSOR_SCHEMA
from esphome.config import Config, validate_config
from esphome.const import (
    CONF_DEVICE,
    CONF_DEVICES,
    CONF_ID,
    CONF_NAME,
    CONF_OPTIONS,
    CONF_TIMEOUT,
    CONF_UPDATE_INTERVAL,
)
from esphome.core import CORE
from esphome.types import ConfigType

from ..helpers import get_define_value

# The widest address the identifier can carry: the message type takes the bits above it.
WIDEST_ADDRESS = 0x7FFFFF


def _hub(**overrides: object) -> ConfigType:
    return {CONF_ID: "mb", CONF_CANBUS_ID: "mb_can", **overrides}


def _sensor(**overrides: object) -> ConfigType:
    return {
        CONF_NAME: "Battery voltage",
        CONF_MASTERBUS_DEVICE_ID: "mb_battery",
        CONF_PARAM: 0x0001,
        **overrides,
    }


def _validated(path: Path) -> Config:
    """Run the whole configuration pipeline over a fixture, errors and all."""
    CORE.config_path = path
    return validate_config(yaml_util.load_yaml(path), {})


def _error_messages(result: Config) -> str:
    return "\n".join(str(error) for error in result.errors)


# The hub


def test_hub_requires_a_canbus_id() -> None:
    """The hub does not own CAN hardware, so it is useless without a bus to listen on."""
    with pytest.raises(cv.Invalid, match="canbus"):
        CONFIG_SCHEMA({CONF_ID: "mb"})


def test_hub_without_devices_accepted() -> None:
    """A hub with nothing declared under it is how a scan is run on an unknown bus."""
    CONFIG_SCHEMA(_hub())


def test_scan_is_an_action_not_an_option() -> None:
    """A scan at boot is the action in on_boot; the hub has no switch of its own for it."""
    with pytest.raises(cv.Invalid, match="scan"):
        CONFIG_SCHEMA(_hub(scan=True))


def test_polling_is_off_unless_asked_for() -> None:
    """A device answers only when asked, but a first look at a live bus must stay silent.

    Polling belongs to the entity: a PollingComponent with no update_interval never runs, so
    leaving the key out is what keeps a freshly configured entity from transmitting.
    """
    assert CONF_UPDATE_INTERVAL not in SENSOR_SCHEMA(_sensor())


def test_update_interval_accepted_on_an_entity() -> None:
    config = SENSOR_SCHEMA(_sensor(**{CONF_UPDATE_INTERVAL: "10s"}))
    assert config[CONF_UPDATE_INTERVAL].total_milliseconds == 10000


def test_hub_takes_no_update_interval() -> None:
    """Nothing to coalesce means nothing for the hub to schedule; the entities own the cadence."""
    with pytest.raises(cv.Invalid, match=CONF_UPDATE_INTERVAL):
        CONFIG_SCHEMA(_hub(**{CONF_UPDATE_INTERVAL: "10s"}))


def test_diagnostics_default_to_off() -> None:
    """Nothing is transmitted and nothing is logged until the user asks for it."""
    config = CONFIG_SCHEMA(_hub())
    assert config[CONF_LOG_ALL_FRAMES] is False


def test_hub_speaks_as_the_vendor_library_unless_given_an_address() -> None:
    assert CONFIG_SCHEMA(_hub())["address"] == 0x500001


def test_hub_address_needs_a_device_class() -> None:
    """The top five bits say what kind of node an address belongs to; none is class 0."""
    with pytest.raises(cv.Invalid, match="class"):
        CONFIG_SCHEMA(_hub(address=0x012345))


def test_hub_address_beyond_the_identifier_names_the_address() -> None:
    """The hub's own key is the one named, and the address reads the way a scan prints one."""
    with pytest.raises(cv.Invalid) as error:
        CONFIG_SCHEMA(_hub(address=0x900000))
    assert "'address' must be" in str(error.value)
    assert "0x900000" in str(error.value)
    assert "0x7FFFFF" in str(error.value)


def test_device_address_beyond_the_identifier_names_the_device() -> None:
    with pytest.raises(cv.Invalid, match="'device' must be"):
        DEVICE_SCHEMA({CONF_ID: "bat", CONF_DEVICE: 0x900000})


def test_hub_address_may_not_be_a_declared_device() -> None:
    """A device does not answer a node request carrying its own address."""
    with pytest.raises(cv.Invalid, match="declared device"):
        CONFIG_SCHEMA(
            _hub(
                address=0x6C1001,
                **{CONF_DEVICES: [{CONF_ID: "bat", CONF_DEVICE: 0x6C1001}]},
            )
        )


def test_node_requests_are_off_unless_asked_for() -> None:
    """Another node usually asks already; a hub only takes it on when the user says so."""
    assert CONF_NODE_REQUEST_INTERVAL not in CONFIG_SCHEMA(_hub())


def test_node_request_interval_accepted() -> None:
    config = CONFIG_SCHEMA(_hub(**{CONF_NODE_REQUEST_INTERVAL: "10s"}))
    assert config[CONF_NODE_REQUEST_INTERVAL].total_milliseconds == 10000


@pytest.mark.parametrize("interval", ["0s", "500ms"])
def test_node_request_interval_below_a_second_rejected(interval: str) -> None:
    """Every device answers every request, three times over; faster than a second only floods."""
    with pytest.raises(cv.Invalid, match=CONF_NODE_REQUEST_INTERVAL):
        CONFIG_SCHEMA(_hub(**{CONF_NODE_REQUEST_INTERVAL: interval}))


# Device identifiers


@pytest.mark.parametrize("address", [0x0, 0x6C1001, 0x501001, WIDEST_ADDRESS])
def test_device_identifier_within_the_extended_can_range_accepted(address: int) -> None:
    """Anything that fits the address field is a possible device."""
    config = DEVICE_SCHEMA({CONF_ID: "mb_battery", CONF_DEVICE: address})
    assert config[CONF_DEVICE] == address


@pytest.mark.parametrize("address", [0x800000, 0x6C10010])
def test_device_identifier_beyond_the_extended_can_range_rejected(address: int) -> None:
    """23 bits is the hard ceiling, and the message says so with the value."""
    with pytest.raises(cv.Invalid, match=f"0x{address:06X}"):
        DEVICE_SCHEMA({CONF_ID: "mb_battery", CONF_DEVICE: address})


def test_device_timeout_of_zero_rejected() -> None:
    """Zero would time the device out on the tick after its first frame, and every tick after."""
    with pytest.raises(cv.Invalid):
        DEVICE_SCHEMA(
            {CONF_ID: "mb_battery", CONF_DEVICE: 0x6C1001, CONF_TIMEOUT: "0s"}
        )


def test_entity_timeout_of_zero_rejected() -> None:
    """Zero reads as "follow the device", which leaving the key out already says."""
    with pytest.raises(cv.Invalid):
        SENSOR_SCHEMA(_sensor(**{CONF_TIMEOUT: "0s"}))


def test_duplicate_device_identifiers_rejected() -> None:
    """Two declarations of one identifier would give two sets of entities on one source."""
    with pytest.raises(cv.Invalid, match="declared twice"):
        CONFIG_SCHEMA(
            _hub(
                **{
                    CONF_DEVICES: [
                        {CONF_ID: "mb_battery", CONF_DEVICE: 0x6C1001},
                        {CONF_ID: "mb_spare", CONF_DEVICE: 0x6C1001},
                    ]
                }
            )
        )


def test_distinct_device_identifiers_accepted() -> None:
    """Six identical battery blocks are six declarations, one per identifier."""
    config = CONFIG_SCHEMA(
        _hub(
            **{
                CONF_DEVICES: [
                    {CONF_ID: "mb_battery", CONF_DEVICE: 0x6C1001},
                    {CONF_ID: "mb_charger", CONF_DEVICE: 0x501001},
                ]
            }
        )
    )
    assert len(config[CONF_DEVICES]) == 2


# Entity addressing


def test_entity_defaults_to_the_monitoring_tab() -> None:
    """Monitoring is where every value a user wants lives, so it costs no extra key."""
    config = SENSOR_SCHEMA(_sensor())
    assert str(config[CONF_TAB].enum_value).endswith("MASTERBUS_TAB_MONITORING")


@pytest.mark.parametrize("tab", ["alarm", "history", "configuration"])
def test_entity_accepts_the_other_tabs(tab: str) -> None:
    """Every tab has a message that reads a value, and an entity may sit on any of them."""
    SENSOR_SCHEMA(_sensor(**{CONF_TAB: tab}))


def test_entity_rejects_the_bootloader_tab() -> None:
    """Firmware update over MasterBus is not something this component goes near."""
    with pytest.raises(cv.Invalid, match="bootloader"):
        SENSOR_SCHEMA(_sensor(**{CONF_TAB: "bootloader"}))


@pytest.mark.parametrize("param", [0x10000, 0xFFFFFFFF])
def test_param_beyond_two_bytes_rejected(param: int) -> None:
    """A field number occupies two bytes of the monitoring broadcast."""
    with pytest.raises(cv.Invalid):
        SENSOR_SCHEMA(_sensor(**{CONF_PARAM: param}))


# Declared value types


def test_sensor_defaults_to_float() -> None:
    config = SENSOR_SCHEMA(_sensor())
    assert str(config[CONF_VALUE_TYPE].enum_value).endswith(
        "MASTERBUS_VALUE_TYPE_FLOAT"
    )


def test_binary_sensor_defaults_to_boolean() -> None:
    config = BINARY_SENSOR_SCHEMA(
        {
            CONF_NAME: "Charger state",
            CONF_MASTERBUS_DEVICE_ID: "mb_charger",
            CONF_PARAM: 0x0015,
        }
    )
    assert str(config[CONF_VALUE_TYPE].enum_value).endswith(
        "MASTERBUS_VALUE_TYPE_BOOLEAN"
    )


@pytest.mark.parametrize("value_type", ["boolean", "list_option"])
def test_sensor_accepts_the_types_it_can_render(value_type: str) -> None:
    SENSOR_SCHEMA(_sensor(**{CONF_VALUE_TYPE: value_type}))


@pytest.mark.parametrize("value_type", ["text", "time", "date"])
def test_sensor_rejects_types_it_cannot_render(value_type: str) -> None:
    """A text field read as a sensor would be a misreading, not a conversion."""
    with pytest.raises(cv.Invalid):
        SENSOR_SCHEMA(_sensor(**{CONF_VALUE_TYPE: value_type}))


@pytest.mark.parametrize("value_type", ["list_option", "device_id", "eventable"])
def test_text_sensor_shows_a_list_entry_by_the_names_it_is_given(
    value_type: str,
) -> None:
    """A list field answers with an index; the names come from a scan, in the device's order."""
    config = TEXT_SENSOR_SCHEMA(
        _sensor(**{CONF_VALUE_TYPE: value_type, CONF_OPTIONS: ["Off", "On"]})
    )
    assert config[CONF_OPTIONS] == ["Off", "On"]


@pytest.mark.parametrize("value_type", ["list_option", "device_id", "eventable"])
def test_text_sensor_on_a_list_field_needs_its_options(value_type: str) -> None:
    with pytest.raises(cv.Invalid, match=CONF_OPTIONS):
        TEXT_SENSOR_SCHEMA(_sensor(**{CONF_VALUE_TYPE: value_type}))


@pytest.mark.parametrize("value_type", ["text", "time", "date"])
def test_text_sensor_with_its_own_text_takes_no_options(value_type: str) -> None:
    with pytest.raises(cv.Invalid, match=CONF_OPTIONS):
        TEXT_SENSOR_SCHEMA(
            _sensor(**{CONF_VALUE_TYPE: value_type, CONF_OPTIONS: ["A"]})
        )


@pytest.mark.parametrize("value_type", ["list_option", "device_id", "eventable"])
def test_select_takes_every_kind_of_list(value_type: str) -> None:
    """A device list and an event field write the index of an entry, as a list option does."""
    SELECT_SCHEMA(_sensor(**{CONF_VALUE_TYPE: value_type, CONF_OPTIONS: ["A", "B"]}))


def test_button_takes_no_update_interval() -> None:
    """A push button carries no state, so there is nothing to poll for."""
    with pytest.raises(cv.Invalid, match="no value to poll"):
        BUTTON_SCHEMA(_sensor(**{CONF_UPDATE_INTERVAL: "10s"}))


@pytest.mark.parametrize("value_type", ["float", "list_option", "text"])
def test_switch_takes_only_a_boolean(value_type: str) -> None:
    with pytest.raises(cv.Invalid, match=CONF_VALUE_TYPE):
        SWITCH_SCHEMA(_sensor(**{CONF_VALUE_TYPE: value_type}))


def test_text_sensor_options_may_not_be_empty() -> None:
    with pytest.raises(cv.Invalid, match="at least 1"):
        TEXT_SENSOR_SCHEMA(
            _sensor(**{CONF_VALUE_TYPE: "list_option", CONF_OPTIONS: []})
        )


def test_select_needs_an_option() -> None:
    with pytest.raises(cv.Invalid, match="at least 1"):
        SELECT_SCHEMA(_sensor(**{CONF_OPTIONS: []}))


def test_select_options_must_differ() -> None:
    """A select writes the index of the option chosen, so a second copy would write the first's."""
    with pytest.raises(cv.Invalid, match="'Float' is listed twice"):
        SELECT_SCHEMA(_sensor(**{CONF_OPTIONS: ["Bulk", "Float", "Float"]}))


def test_number_range_may_not_be_inverted() -> None:
    with pytest.raises(cv.Invalid, match="min_value"):
        NUMBER_SCHEMA(_sensor(min_value=10, max_value=0, step=1))


def test_number_range_accepted() -> None:
    NUMBER_SCHEMA(_sensor(min_value=0, max_value=100, step=1))


@pytest.mark.parametrize(
    "info",
    [
        "article_number",
        "serial_number",
        "product_name",
        "revision_code",
        "firmware",
        "class",
    ],
)
def test_text_sensor_shows_what_a_device_says_about_itself(info: str) -> None:
    """No field number: the device answers these for itself, and its address gives its class."""
    config = TEXT_SENSOR_SCHEMA(
        {CONF_NAME: "Battery", CONF_MASTERBUS_DEVICE_ID: "mb_battery", "info": info}
    )
    assert config["info"] == info
    assert CONF_PARAM not in config


def test_text_sensor_info_takes_no_field_number() -> None:
    with pytest.raises(cv.Invalid):
        TEXT_SENSOR_SCHEMA(_sensor(info="serial_number"))


def test_text_sensor_info_must_be_one_the_device_answers() -> None:
    with pytest.raises(cv.Invalid, match="info"):
        TEXT_SENSOR_SCHEMA(
            {CONF_NAME: "x", CONF_MASTERBUS_DEVICE_ID: "mb_battery", "info": "colour"}
        )


# Cross references, over the whole configuration


def test_entity_referencing_an_undeclared_device_rejected(
    component_config_path: Callable[[str], Path],
) -> None:
    """A device renamed under the hub must not leave entities pointing at nothing."""
    result = _validated(component_config_path("unknown_device.yaml"))
    assert result.errors
    assert "mb_inverter" in _error_messages(result)


def test_hub_without_a_canbus_id_names_the_missing_key(
    component_config_path: Callable[[str], Path],
) -> None:
    result = _validated(component_config_path("missing_canbus.yaml"))
    assert result.errors
    assert CONF_CANBUS_ID in _error_messages(result)


def test_two_hubs_on_one_bus_may_not_share_an_address(
    component_config_path: Callable[[str], Path],
) -> None:
    """Both would transmit under one identity and take each other's answers as their own."""
    result = _validated(component_config_path("two_hubs_one_address.yaml"))
    assert result.errors
    assert "mb_port" in _error_messages(result)
    assert "0x500001" in _error_messages(result)


def test_two_hubs_on_one_bus_with_their_own_addresses_accepted(
    component_config_path: Callable[[str], Path],
) -> None:
    result = _validated(component_config_path("two_hubs.yaml"))
    assert not result.errors, _error_messages(result)


def test_polling_slower_than_the_device_timeout_warns(
    component_config_path: Callable[[str], Path],
    caplog: pytest.LogCaptureFixture,
) -> None:
    """Nothing carries the device's address between polls, so it is judged on a poll it outlives."""
    result = _validated(component_config_path("slow_poll.yaml"))
    assert not result.errors
    assert "go offline between answers" in caplog.text


def test_polling_exactly_as_slow_as_the_timeout_warns(
    component_config_path: Callable[[str], Path],
    caplog: pytest.LogCaptureFixture,
) -> None:
    """The answer and the timeout arrive together, so the device can drop between them."""
    result = _validated(component_config_path("poll_as_slow_as_the_timeout.yaml"))
    assert not result.errors
    assert "go offline between answers" in caplog.text


def test_the_fastest_poller_decides_whether_to_warn(
    component_config_path: Callable[[str], Path],
    caplog: pytest.LogCaptureFixture,
) -> None:
    result = _validated(component_config_path("poll_fastest.yaml"))
    assert not result.errors
    assert "go offline between answers" not in caplog.text


def test_a_poller_that_never_runs_is_no_reason_to_warn(
    component_config_path: Callable[[str], Path],
    caplog: pytest.LogCaptureFixture,
) -> None:
    result = _validated(component_config_path("poll_never.yaml"))
    assert not result.errors
    assert "go offline between answers" not in caplog.text


def test_node_requests_faster_than_the_timeout_keep_the_warning_quiet(
    component_config_path: Callable[[str], Path],
    caplog: pytest.LogCaptureFixture,
) -> None:
    """Each announcement answering the hub's own node request keeps the device online too."""
    result = _validated(component_config_path("slow_poll_node_requests.yaml"))
    assert not result.errors
    assert "go offline between answers" not in caplog.text


# What the configuration compiles into


def test_diagnostics_emit_no_defines_when_disabled(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """A finished installation carries neither the scan nor the frame log."""
    generate_main(component_config_path("diagnostics_off.yaml"))
    assert get_define_value("USE_MASTERBUS_SCAN") is None
    assert get_define_value("USE_MASTERBUS_LOG_ALL_FRAMES") is None
    assert get_define_value("MASTERBUS_SCAN_MAX_DEVICES") is None


def test_diagnostics_emit_defines_when_enabled(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    generate_main(component_config_path("diagnostics_on.yaml"))
    assert get_define_value("USE_MASTERBUS_SCAN") is not None
    assert get_define_value("USE_MASTERBUS_LOG_ALL_FRAMES") is not None
    # The scan's storage is sized by a define, so it goes with it.
    assert get_define_value("MASTERBUS_SCAN_MAX_DEVICES") == "32"


def test_a_scan_action_compiles_the_scan_in_without_scanning_at_boot(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """A button that starts a scan needs the walk, not a walk at every boot."""
    main_cpp = generate_main(component_config_path("scan_on_demand.yaml"))
    assert get_define_value("USE_MASTERBUS_SCAN") is not None
    assert get_define_value("MASTERBUS_SCAN_MAX_DEVICES") == "32"
    assert "->start_scan();" in main_cpp


def test_node_requests_emit_no_define_when_off(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    generate_main(component_config_path("diagnostics_off.yaml"))
    assert get_define_value("USE_MASTERBUS_NODE_REQUEST") is None


def test_node_request_interval_is_scheduled(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    main_cpp = generate_main(component_config_path("node_requests.yaml"))
    assert get_define_value("USE_MASTERBUS_NODE_REQUEST") is not None
    assert "mb->set_node_request_interval(10000);" in main_cpp
    # Asking on a timer is not a scan: nothing of the scan is built in with it.
    assert get_define_value("USE_MASTERBUS_SCAN") is None


def test_storage_counts_follow_the_declared_devices_and_entities(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """Both lists are sized at code generation time, so neither reallocates at runtime."""
    generate_main(component_config_path("full.yaml"))
    assert get_define_value("MASTERBUS_DEVICE_COUNT") == "2"
    assert get_define_value("MASTERBUS_ENTITY_COUNT") == "3"


def test_hub_without_devices_emits_no_storage_counts(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """No devices, no defines - the guarded storage compiles out."""
    generate_main(component_config_path("diagnostics_off.yaml"))
    assert get_define_value("MASTERBUS_DEVICE_COUNT") is None
    assert get_define_value("MASTERBUS_ENTITY_COUNT") is None


def test_a_class_sensor_alone_compiles_the_information_sensor_in(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """The class needs no read, so it asks for no slot, but its sensor still has to exist."""
    main_cpp = generate_main(component_config_path("info_class_only.yaml"))
    assert get_define_value("USE_MASTERBUS_INFO") is not None
    assert get_define_value("MASTERBUS_INFO_COUNT") is None
    assert "MasterbusInfoTextSensor(" in main_cpp


def test_each_hub_sizes_its_lists_for_its_own_devices(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """Every hub's lists are declared at one size, so the size is the largest hub's, not the sum."""
    generate_main(component_config_path("two_hubs.yaml"))
    assert get_define_value("MASTERBUS_DEVICE_COUNT") == "3"
    assert get_define_value("MASTERBUS_ENTITY_COUNT") == "2"
    assert get_define_value("MASTERBUS_INFO_COUNT") == "1"
    assert get_define_value("MASTERBUS_UNKNOWN_FRAME_COUNT") == "1"


# Every schema branch, checked in the code it generates. A missing registration still compiles and
# silently does nothing, so it is the generated call that is asserted.

FIELD_ENTITIES = (
    ("mb_battery", "battery_voltage"),
    ("mb_battery", "battery_low"),
    ("mb_battery", "battery_label"),
    ("mb_display", "display_language"),
    ("mb_display", "display_backlight"),
    ("mb_display", "display_page"),
    ("mb_display", "display_relay"),
    ("mb_battery", "battery_close_relay"),
)


def test_the_hub_and_its_devices_are_generated_as_configured(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    main_cpp = generate_main(component_config_path("codegen.yaml"))
    assert "mb->set_address(0x381001);" in main_cpp
    assert (
        "mb->add_on_unknown_frame_callback("
        "TriggerForwarder<std::vector<uint8_t>, uint8_t, uint32_t>" in main_cpp
    )
    assert get_define_value("MASTERBUS_UNKNOWN_FRAME_COUNT") == "1"
    assert "new(mb_battery) masterbus::MasterbusDevice(mb, 0x6C1001);" in main_cpp
    # A device's timeout defaults to a minute.
    assert "mb_battery->set_timeout(60000);" in main_cpp
    assert "mb_display->set_timeout(300000);" in main_cpp
    assert "mb->register_device(mb_battery);" in main_cpp
    assert "mb->register_device(mb_display);" in main_cpp
    assert "mb_battery->add_on_online_callback(TriggerForwarder<>" in main_cpp
    assert "mb_battery->add_on_offline_callback(TriggerForwarder<>" in main_cpp


def test_a_hub_speaking_as_the_vendor_library_sets_no_address(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """The class already starts with the vendor library's address."""
    main_cpp = generate_main(component_config_path("full.yaml"))
    assert "set_address(" not in main_cpp


@pytest.mark.parametrize(("device", "entity"), FIELD_ENTITIES)
def test_every_field_entity_is_registered_with_its_device(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
    device: str,
    entity: str,
) -> None:
    main_cpp = generate_main(component_config_path("codegen.yaml"))
    assert f"{device}->register_entity({entity});" in main_cpp
    assert f"App.register_component_({entity}," in main_cpp


def test_field_entities_carry_their_address_tab_and_type(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    main_cpp = generate_main(component_config_path("codegen.yaml"))
    assert get_define_value("MASTERBUS_ENTITY_COUNT") == str(len(FIELD_ENTITIES))
    assert (
        "masterbus::MasterbusBinarySensor(mb_battery, 0x02, "
        "masterbus::MasterbusTab::MASTERBUS_TAB_ALARM, "
        "masterbus::MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);" in main_cpp
    )
    assert (
        "masterbus::MasterbusSelect(mb_display, 0x2C, "
        "masterbus::MasterbusTab::MASTERBUS_TAB_CONFIGURATION, "
        "masterbus::MasterbusValueType::MASTERBUS_VALUE_TYPE_LIST_OPTION);" in main_cpp
    )
    # Only the entity given a timeout of its own gets one.
    assert "battery_voltage->set_stale_timeout(30000);" in main_cpp
    assert main_cpp.count("set_stale_timeout(") == 1
    assert "battery_voltage->set_update_interval(10000);" in main_cpp


def test_list_entities_carry_their_options_and_numbers_their_range(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    main_cpp = generate_main(component_config_path("codegen.yaml"))
    assert "display_language->set_options(masterbus_text_options, 2);" in main_cpp
    assert '"Nederlands"' in main_cpp
    assert "display_page->traits.set_options_static(select_options, 2);" in main_cpp
    assert "display_backlight->traits.set_min_value(5.0f);" in main_cpp
    assert "display_backlight->traits.set_max_value(95.0f);" in main_cpp
    assert "display_backlight->traits.set_step(5.0f);" in main_cpp


def test_a_text_field_compiles_the_string_read_in(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    generate_main(component_config_path("codegen.yaml"))
    assert get_define_value("USE_MASTERBUS_TEXT") is not None


def test_no_text_field_leaves_the_string_read_out(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    generate_main(component_config_path("full.yaml"))
    assert get_define_value("USE_MASTERBUS_TEXT") is None
    assert get_define_value("USE_MASTERBUS_INFO") is None


def test_information_read_off_the_bus_is_registered_and_the_class_is_not(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    main_cpp = generate_main(component_config_path("codegen.yaml"))
    assert get_define_value("USE_MASTERBUS_INFO") is not None
    assert get_define_value("MASTERBUS_INFO_COUNT") == "1"
    assert "mb_battery->register_info_entity(battery_serial);" in main_cpp
    assert "register_info_entity(battery_class)" not in main_cpp
    for entity in ("battery_serial", "battery_class"):
        assert f"App.register_component_({entity}," in main_cpp
        assert f"App.register_text_sensor({entity}," in main_cpp
    assert (
        "masterbus::MasterbusInfoTextSensor(mb_battery, "
        "masterbus::MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_CLASS);" in main_cpp
    )
