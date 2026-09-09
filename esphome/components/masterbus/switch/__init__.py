import esphome.codegen as cg
from esphome.components import switch
from esphome.types import ConfigType

from .. import entity_schema, masterbus_ns, new_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusSwitch = masterbus_ns.class_(
    "MasterbusSwitch", switch.Switch, cg.PollingComponent
)

CONFIG_SCHEMA = switch.switch_schema(MasterbusSwitch).extend(
    entity_schema(value_types=("boolean",), default_value_type="boolean")
)


async def to_code(config: ConfigType) -> None:
    await new_entity(switch.new_switch, config)
