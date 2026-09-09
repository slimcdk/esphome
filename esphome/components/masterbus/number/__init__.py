import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import CONF_MAX_VALUE, CONF_MIN_VALUE, CONF_STEP
from esphome.types import ConfigType

from .. import entity_args, entity_schema, masterbus_ns, register_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusNumber = masterbus_ns.class_(
    "MasterbusNumber", number.Number, cg.PollingComponent
)

# A scan reports the minimum, maximum and step size of every field, so these are a copy from the
# log rather than something the user has to work out.
CONFIG_SCHEMA = (
    number.number_schema(MasterbusNumber)
    .extend(entity_schema(value_types=("float",), default_value_type="float"))
    .extend(
        {
            cv.Required(CONF_MIN_VALUE): cv.float_,
            cv.Required(CONF_MAX_VALUE): cv.float_,
            cv.Required(CONF_STEP): cv.positive_float,
        }
    )
)


async def to_code(config: ConfigType) -> None:
    var = await number.new_number(
        config,
        *await entity_args(config),
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    await register_entity(var, config)
