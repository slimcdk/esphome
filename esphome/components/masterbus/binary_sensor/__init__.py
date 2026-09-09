import esphome.codegen as cg
from esphome.components import binary_sensor
from esphome.types import ConfigType

from .. import entity_args, entity_schema, masterbus_ns, register_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusBinarySensor = masterbus_ns.class_(
    "MasterbusBinarySensor", binary_sensor.BinarySensor, cg.PollingComponent
)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(MasterbusBinarySensor).extend(
    entity_schema(value_types=("boolean",), default_value_type="boolean")
)


async def to_code(config: ConfigType) -> None:
    var = await binary_sensor.new_binary_sensor(config, *await entity_args(config))
    await register_entity(var, config)
