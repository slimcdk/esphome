import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_OPTIONS
from esphome.types import ConfigType

from .. import entity_args, entity_schema, masterbus_ns, register_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusSelect = masterbus_ns.class_(
    "MasterbusSelect", select.Select, cg.PollingComponent
)

# The option names come from the device's own string table, which a scan reports. They are listed
# here in the order the device numbers them, so the reported index picks the right one.
CONFIG_SCHEMA = (
    select.select_schema(MasterbusSelect)
    .extend(
        entity_schema(value_types=("list_option",), default_value_type="list_option")
    )
    .extend({cv.Required(CONF_OPTIONS): cv.ensure_list(cv.string_strict)})
)


async def to_code(config: ConfigType) -> None:
    var = await select.new_select(
        config, *await entity_args(config), options=config[CONF_OPTIONS]
    )
    await register_entity(var, config)
