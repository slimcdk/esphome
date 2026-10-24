import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import CONF_MAX_VALUE, CONF_MIN_VALUE, CONF_STEP
from esphome.types import ConfigType

from .. import entity_schema, masterbus_ns, new_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusNumber = masterbus_ns.class_(
    "MasterbusNumber", number.Number, cg.PollingComponent
)


def _validate_range(config: ConfigType) -> ConfigType:
    if config[CONF_MIN_VALUE] > config[CONF_MAX_VALUE]:
        raise cv.Invalid(
            f"'{CONF_MIN_VALUE}' {config[CONF_MIN_VALUE]} is above '{CONF_MAX_VALUE}' "
            f"{config[CONF_MAX_VALUE]}; a scan reports them as min and max"
        )
    return config


# A scan reports the minimum, maximum and step size of every field, so these are a copy from the
# log rather than something the user has to work out.
CONFIG_SCHEMA = cv.All(
    number.number_schema(MasterbusNumber)
    .extend(entity_schema(value_types=("float",), default_value_type="float"))
    .extend(
        {
            cv.Required(CONF_MIN_VALUE): cv.float_,
            cv.Required(CONF_MAX_VALUE): cv.float_,
            cv.Required(CONF_STEP): cv.positive_float,
        }
    ),
    _validate_range,
)


async def to_code(config: ConfigType) -> None:
    await new_entity(
        number.new_number,
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
