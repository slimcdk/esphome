import esphome.codegen as cg
from esphome.components import switch
from esphome.types import ConfigType

from .. import entity_args, entity_schema, masterbus_ns, register_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusSwitch = masterbus_ns.class_(
    "MasterbusSwitch", switch.Switch, cg.PollingComponent
)

CONFIG_SCHEMA = switch.switch_schema(MasterbusSwitch).extend(
    entity_schema(value_types=("boolean",), default_value_type="boolean")
)


async def to_code(config: ConfigType) -> None:
    var = await switch.new_switch(config, *await entity_args(config))
    await register_entity(var, config)
