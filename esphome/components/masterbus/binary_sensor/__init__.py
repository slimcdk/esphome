from esphome.components import binary_sensor
from esphome.types import ConfigType

from .. import MasterbusEntity, entity_schema, masterbus_ns, new_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusBinarySensor = masterbus_ns.class_(
    "MasterbusBinarySensor", binary_sensor.BinarySensor, MasterbusEntity
)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(MasterbusBinarySensor).extend(
    entity_schema(value_types=("boolean",), default_value_type="boolean")
)


async def to_code(config: ConfigType) -> None:
    await new_entity(binary_sensor.new_binary_sensor, config)
