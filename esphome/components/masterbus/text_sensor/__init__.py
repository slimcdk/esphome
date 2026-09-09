import esphome.codegen as cg
from esphome.components import text_sensor
from esphome.types import ConfigType

from .. import entity_schema, masterbus_ns, new_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusTextSensor = masterbus_ns.class_(
    "MasterbusTextSensor", text_sensor.TextSensor, cg.PollingComponent
)

CONFIG_SCHEMA = text_sensor.text_sensor_schema(MasterbusTextSensor).extend(
    entity_schema(value_types=("text", "time", "date"), default_value_type="text")
)


async def to_code(config: ConfigType) -> None:
    await new_entity(text_sensor.new_text_sensor, config)
