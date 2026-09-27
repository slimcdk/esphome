import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_CONNECTIVITY, ENTITY_CATEGORY_DIAGNOSTIC
from esphome.types import ConfigType

from .. import BmsEmulator

CONF_BMS_EMULATOR_ID = "bms_emulator_id"
CONF_INVERTER_ONLINE = "inverter_online"

DEPENDENCIES = ["bms_emulator"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BMS_EMULATOR_ID): cv.use_id(BmsEmulator),
        # True while the inverter's own frame keeps arriving.
        cv.Optional(CONF_INVERTER_ONLINE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    emulator = await cg.get_variable(config[CONF_BMS_EMULATOR_ID])
    await binary_sensor.new_sub_binary_sensor(
        config, CONF_INVERTER_ONLINE, emulator.set_inverter_online_binary_sensor
    )
