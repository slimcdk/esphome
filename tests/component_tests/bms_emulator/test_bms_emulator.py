"""Tests for bms_emulator configuration validation and code generation.

What the component tells an inverter decides how the inverter charges a battery, so the schema has
to refuse a limit that makes no sense before it is ever encoded.
"""

from collections.abc import Callable
from pathlib import Path

import pytest

from esphome import config_validation as cv
from esphome.components.bms_emulator import (
    CONF_CHARGE_CURRENT_LIMIT,
    CONF_CHARGE_ENABLED,
    CONF_CHARGE_VOLTAGE,
    CONF_CURRENT_ID,
    CONF_DISCHARGE_CURRENT_LIMIT,
    CONF_DISCHARGE_ENABLED,
    CONF_DISCHARGE_VOLTAGE,
    CONF_INVERTER_FRAME_ID,
    CONF_INVERTER_TIMEOUT,
    CONF_MODULE_COUNT,
    CONF_STATE_OF_CHARGE_ID,
    CONF_STATE_OF_HEALTH,
    CONF_TEMPERATURE_ID,
    CONF_VOLTAGE_ID,
    CONFIG_SCHEMA,
)
from esphome.components.canbus import CONF_CANBUS_ID
from esphome.const import CONF_ID, CONF_TYPE, CONF_UPDATE_INTERVAL
from esphome.types import ConfigType


def _emulator(**overrides: object) -> ConfigType:
    return {
        CONF_ID: "bms",
        CONF_TYPE: "growatt_pylontech",
        CONF_CANBUS_ID: "inverter_can",
        CONF_VOLTAGE_ID: "pack_voltage",
        CONF_CURRENT_ID: "pack_current",
        CONF_STATE_OF_CHARGE_ID: "pack_soc",
        CONF_TEMPERATURE_ID: "pack_temperature",
        CONF_CHARGE_VOLTAGE: 55.0,
        CONF_DISCHARGE_VOLTAGE: 48.0,
        CONF_CHARGE_CURRENT_LIMIT: 100,
        CONF_DISCHARGE_CURRENT_LIMIT: 100,
        **overrides,
    }


def test_minimal_configuration_takes_the_documented_defaults() -> None:
    config = CONFIG_SCHEMA(_emulator())
    assert config[CONF_STATE_OF_HEALTH] == 100
    assert config[CONF_CHARGE_ENABLED] is True
    assert config[CONF_DISCHARGE_ENABLED] is True
    assert config[CONF_MODULE_COUNT] == 1
    assert config[CONF_INVERTER_FRAME_ID] == 0x301
    assert config[CONF_INVERTER_TIMEOUT].total_milliseconds == 5000
    assert config[CONF_UPDATE_INTERVAL].total_milliseconds == 1000


@pytest.mark.parametrize(
    "key",
    [
        CONF_TYPE,
        CONF_VOLTAGE_ID,
        CONF_CURRENT_ID,
        CONF_STATE_OF_CHARGE_ID,
        CONF_TEMPERATURE_ID,
        CONF_CHARGE_VOLTAGE,
        CONF_DISCHARGE_VOLTAGE,
        CONF_CHARGE_CURRENT_LIMIT,
        CONF_DISCHARGE_CURRENT_LIMIT,
    ],
)
def test_required_keys(key: str) -> None:
    config = _emulator()
    del config[key]
    with pytest.raises(cv.Invalid, match=key):
        CONFIG_SCHEMA(config)


def test_units_are_accepted_on_voltages_and_currents() -> None:
    config = CONFIG_SCHEMA(
        _emulator(
            **{
                CONF_CHARGE_VOLTAGE: "55.0V",
                CONF_DISCHARGE_VOLTAGE: "48V",
                CONF_CHARGE_CURRENT_LIMIT: "20A",
                CONF_DISCHARGE_CURRENT_LIMIT: "100 A",
            }
        )
    )
    assert config[CONF_CHARGE_VOLTAGE] == 55.0
    assert config[CONF_DISCHARGE_CURRENT_LIMIT] == 100.0


@pytest.mark.parametrize("key", [CONF_CHARGE_VOLTAGE, CONF_DISCHARGE_VOLTAGE])
def test_voltages_are_not_negative(key: str) -> None:
    with pytest.raises(cv.Invalid, match=key):
        CONFIG_SCHEMA(_emulator(**{key: "-1V"}))


def test_unknown_type_rejected() -> None:
    with pytest.raises(cv.Invalid, match=CONF_TYPE):
        CONFIG_SCHEMA(_emulator(**{CONF_TYPE: "pylontech_rs485"}))


@pytest.mark.parametrize(
    "key", [CONF_CHARGE_CURRENT_LIMIT, CONF_DISCHARGE_CURRENT_LIMIT]
)
def test_current_limits_are_magnitudes(key: str) -> None:
    """The frame set decides the sign on the wire; a negative limit here is a mistake."""
    with pytest.raises(cv.Invalid, match=key):
        CONFIG_SCHEMA(_emulator(**{key: -100}))


@pytest.mark.parametrize("value", [-1, 101])
def test_state_of_health_is_a_percentage(value: int) -> None:
    with pytest.raises(cv.Invalid, match=CONF_STATE_OF_HEALTH):
        CONFIG_SCHEMA(_emulator(**{CONF_STATE_OF_HEALTH: value}))


@pytest.mark.parametrize("value", [0, 256])
def test_module_count_fits_its_byte(value: int) -> None:
    with pytest.raises(cv.Invalid, match=CONF_MODULE_COUNT):
        CONFIG_SCHEMA(_emulator(**{CONF_MODULE_COUNT: value}))


def test_inverter_frame_is_a_standard_identifier() -> None:
    with pytest.raises(cv.Invalid, match=CONF_INVERTER_FRAME_ID):
        CONFIG_SCHEMA(_emulator(**{CONF_INVERTER_FRAME_ID: 0x800}))


def test_minimal_configuration_generates_the_emulator(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    # The only bus in the configuration is taken when canbus_id is left out, and the four required
    # sensors are passed to the constructor.
    main_cpp = generate_main(component_config_path("minimal.yaml"))
    assert (
        "bms_emulator::BmsEmulator(inverter_can, "
        "bms_emulator::BmsEmulatorType::BMS_EMULATOR_TYPE_GROWATT_PYLONTECH, "
        "pack_voltage, pack_current, pack_soc, pack_temperature)"
    ) in main_cpp
    assert "set_voltage_sensor" not in main_cpp
    assert "bms->set_inverter_frame_id(769);" in main_cpp
    assert "bms->set_inverter_timeout(5000);" in main_cpp
    assert "set_inverter_online_binary_sensor" not in main_cpp


def test_full_configuration_passes_lambdas_and_the_binary_sensor(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    main_cpp = generate_main(component_config_path("full.yaml"))
    assert "bms->set_update_interval(500);" in main_cpp
    assert "return pack_soc->state >= 98 ? 20.0f : 100.0f;" in main_cpp
    assert "bms->set_charge_enabled([]() -> bool {" in main_cpp
    assert "bms->set_module_count(2);" in main_cpp
    assert "bms->set_inverter_frame_id(773);" in main_cpp
    assert "bms->set_inverter_timeout(10000);" in main_cpp
    assert "bms->set_inverter_online_binary_sensor(" in main_cpp
