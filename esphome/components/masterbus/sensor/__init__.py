import esphome.codegen as cg
from esphome.components import sensor
from esphome.types import ConfigType

from .. import entity_args, entity_schema, masterbus_ns, register_entity

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
    var = await sensor.new_sensor(config, *await entity_args(config))
    await register_entity(var, config)
