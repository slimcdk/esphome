import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import CONF_UPDATE_INTERVAL
from esphome.types import ConfigType

from .. import entity_schema, masterbus_ns, new_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusButton = masterbus_ns.class_(
    "MasterbusButton", button.Button, cg.PollingComponent
)

# A push button carries no state, so there is nothing to poll for and no cadence to configure.
CONFIG_SCHEMA = (
    button.button_schema(MasterbusButton)
    .extend(entity_schema(value_types=("boolean",), default_value_type="boolean"))
    .extend(
        {
            cv.Optional(CONF_UPDATE_INTERVAL): cv.invalid(
                "a button has no value to poll for"
            )
        }
    )
)


async def to_code(config: ConfigType) -> None:
    await new_entity(button.new_button, config)
