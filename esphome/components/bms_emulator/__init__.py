import esphome.codegen as cg
from esphome.components import sensor
from esphome.components.canbus import CONF_CANBUS_ID, CanbusComponent
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TYPE
from esphome.cpp_generator import MockObj
from esphome.types import ConfigType

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["canbus", "sensor"]
MULTI_CONF = True

CONF_CHARGE_CURRENT_LIMIT = "charge_current_limit"
CONF_CHARGE_ENABLED = "charge_enabled"
CONF_CHARGE_VOLTAGE = "charge_voltage"
CONF_CURRENT_ID = "current_id"
CONF_DISCHARGE_CURRENT_LIMIT = "discharge_current_limit"
CONF_DISCHARGE_ENABLED = "discharge_enabled"
CONF_DISCHARGE_VOLTAGE = "discharge_voltage"
CONF_INVERTER_FRAME_ID = "inverter_frame_id"
CONF_INVERTER_TIMEOUT = "inverter_timeout"
CONF_MODULE_COUNT = "module_count"
CONF_STATE_OF_CHARGE_ID = "state_of_charge_id"
CONF_STATE_OF_HEALTH = "state_of_health"
CONF_TEMPERATURE_ID = "temperature_id"
CONF_VOLTAGE_ID = "voltage_id"

bms_emulator_ns = cg.esphome_ns.namespace("bms_emulator")
BmsEmulator = bms_emulator_ns.class_("BmsEmulator", cg.PollingComponent)
BmsEmulatorType = bms_emulator_ns.enum("BmsEmulatorType", is_class=True)

TYPES = {
    # The Pylontech low-voltage frame set as a Growatt SPH reads it.
    "growatt_pylontech": BmsEmulatorType.BMS_EMULATOR_TYPE_GROWATT_PYLONTECH,
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BmsEmulator),
        cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
        cv.Required(CONF_CANBUS_ID): cv.use_id(CanbusComponent),
        cv.Required(CONF_VOLTAGE_ID): cv.use_id(sensor.Sensor),
        cv.Required(CONF_CURRENT_ID): cv.use_id(sensor.Sensor),
        cv.Required(CONF_STATE_OF_CHARGE_ID): cv.use_id(sensor.Sensor),
        cv.Required(CONF_TEMPERATURE_ID): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_STATE_OF_HEALTH, default=100): cv.templatable(
            cv.float_range(min=0, max=100)
        ),
        cv.Required(CONF_CHARGE_VOLTAGE): cv.templatable(cv.positive_float),
        cv.Required(CONF_DISCHARGE_VOLTAGE): cv.templatable(cv.positive_float),
        # Both limits are magnitudes; the frame set decides how each is signed on the wire.
        cv.Required(CONF_CHARGE_CURRENT_LIMIT): cv.templatable(cv.positive_float),
        cv.Required(CONF_DISCHARGE_CURRENT_LIMIT): cv.templatable(cv.positive_float),
        cv.Optional(CONF_CHARGE_ENABLED, default=True): cv.templatable(cv.boolean),
        cv.Optional(CONF_DISCHARGE_ENABLED, default=True): cv.templatable(cv.boolean),
        cv.Optional(CONF_MODULE_COUNT, default=1): cv.int_range(min=1, max=255),
        # The frame whose arrival means an inverter is listening: a Growatt sends 0x301.
        cv.Optional(CONF_INVERTER_FRAME_ID, default=0x301): cv.int_range(
            min=0, max=0x7FF
        ),
        cv.Optional(
            CONF_INVERTER_TIMEOUT, default="5s"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.polling_component_schema("1s"))


async def to_code(config: ConfigType) -> None:
    canbus = await cg.get_variable(config[CONF_CANBUS_ID])
    var = cg.new_Pvariable(config[CONF_ID], canbus, config[CONF_TYPE])
    await cg.register_component(var, config)

    cg.add(var.set_voltage_sensor(await cg.get_variable(config[CONF_VOLTAGE_ID])))
    cg.add(var.set_current_sensor(await cg.get_variable(config[CONF_CURRENT_ID])))
    cg.add(
        var.set_state_of_charge_sensor(
            await cg.get_variable(config[CONF_STATE_OF_CHARGE_ID])
        )
    )
    cg.add(
        var.set_temperature_sensor(await cg.get_variable(config[CONF_TEMPERATURE_ID]))
    )

    async def value(key: str, kind: MockObj) -> MockObj:
        return await cg.templatable(config[key], [], kind)

    cg.add(var.set_state_of_health(await value(CONF_STATE_OF_HEALTH, cg.float_)))
    cg.add(var.set_charge_voltage(await value(CONF_CHARGE_VOLTAGE, cg.float_)))
    cg.add(var.set_discharge_voltage(await value(CONF_DISCHARGE_VOLTAGE, cg.float_)))
    cg.add(
        var.set_charge_current_limit(await value(CONF_CHARGE_CURRENT_LIMIT, cg.float_))
    )
    cg.add(
        var.set_discharge_current_limit(
            await value(CONF_DISCHARGE_CURRENT_LIMIT, cg.float_)
        )
    )
    cg.add(var.set_charge_enabled(await value(CONF_CHARGE_ENABLED, cg.bool_)))
    cg.add(var.set_discharge_enabled(await value(CONF_DISCHARGE_ENABLED, cg.bool_)))
    cg.add(var.set_module_count(config[CONF_MODULE_COUNT]))
    cg.add(var.set_inverter_frame_id(config[CONF_INVERTER_FRAME_ID]))
    cg.add(var.set_inverter_timeout(config[CONF_INVERTER_TIMEOUT]))
