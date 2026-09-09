import esphome.codegen as cg
from esphome.components import sensor
from esphome.types import ConfigType

from .. import entity_schema, masterbus_ns, new_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusSensor = masterbus_ns.class_(
    "MasterbusSensor", sensor.Sensor, cg.PollingComponent
)

CONFIG_SCHEMA = sensor.sensor_schema(MasterbusSensor).extend(
    entity_schema(
        value_types=("float", "boolean", "list_option"), default_value_type="float"
    )
)


async def to_code(config: ConfigType) -> None:
    await new_entity(sensor.new_sensor, config)
