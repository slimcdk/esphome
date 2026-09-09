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
    CONF_PARAM,
    CONF_TAB,
    CONFIG_SCHEMA,
    DEVICE_SCHEMA,
)
from esphome.components.masterbus.binary_sensor import (
    CONFIG_SCHEMA as BINARY_SENSOR_SCHEMA,
)
from esphome.components.masterbus.sensor import CONFIG_SCHEMA as SENSOR_SCHEMA
from esphome.config import Config, validate_config
from esphome.const import (
    CONF_DEVICE,
    CONF_DEVICES,
    CONF_ID,
    CONF_NAME,
    CONF_SCAN,
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
    CONFIG_SCHEMA(_hub(**{CONF_SCAN: True}))


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
    assert config[CONF_SCAN] is False
    assert config[CONF_LOG_ALL_FRAMES] is False


# Device identifiers


@pytest.mark.parametrize("address", [0x0, 0x6D56EA, 0x535E30, WIDEST_ADDRESS])
def test_device_identifier_within_the_extended_can_range_accepted(address: int) -> None:
    """Anything that fits the address field is a possible device."""
    config = DEVICE_SCHEMA({CONF_ID: "mb_battery", CONF_DEVICE: address})
    assert config[CONF_DEVICE] == address


@pytest.mark.parametrize("address", [0x800000, 0x6D56EA0])
def test_device_identifier_beyond_the_extended_can_range_rejected(address: int) -> None:
    """23 bits is the hard ceiling, and the message says so with the value."""
    with pytest.raises(cv.Invalid, match=f"{address:#X}"):
        DEVICE_SCHEMA({CONF_ID: "mb_battery", CONF_DEVICE: address})


def test_duplicate_device_identifiers_rejected() -> None:
    """Two declarations of one identifier would give two sets of entities on one source."""
    with pytest.raises(cv.Invalid, match="declared twice"):
        CONFIG_SCHEMA(
            _hub(
                **{
                    CONF_DEVICES: [
                        {CONF_ID: "mb_battery", CONF_DEVICE: 0x6D56EA},
                        {CONF_ID: "mb_spare", CONF_DEVICE: 0x6D56EA},
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
                    {CONF_ID: "mb_battery", CONF_DEVICE: 0x6D56EA},
                    {CONF_ID: "mb_charger", CONF_DEVICE: 0x535E30},
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
    """The address model leaves room for the tabs whose messages are not decoded yet."""
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
        {CONF_NAME: "Relay", CONF_MASTERBUS_DEVICE_ID: "mb_battery", CONF_PARAM: 0x0075}
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
