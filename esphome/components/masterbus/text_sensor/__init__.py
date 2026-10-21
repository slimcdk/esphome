import esphome.codegen as cg
from esphome.components import text_sensor
from esphome.components.const import CONF_VALUE_TYPE
import esphome.config_validation as cv
from esphome.const import CONF_OPTIONS
from esphome.types import ConfigType

from .. import entity_schema, masterbus_ns, new_entity

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusTextSensor = masterbus_ns.class_(
    "MasterbusTextSensor", text_sensor.TextSensor, cg.PollingComponent
)

# A list field answers with the index of an entry, so the text it shows has to be given. A scan
# reports each entry's name, and the converter on the documentation page writes them in.
LIST_VALUE_TYPES = ("list_option", "device_id", "eventable")


def _validate_options(config: ConfigType) -> ConfigType:
    listed = config[CONF_VALUE_TYPE] in LIST_VALUE_TYPES
    if listed and CONF_OPTIONS not in config:
        raise cv.Invalid(
            f"a {config[CONF_VALUE_TYPE]} field answers with the number of an entry, so "
            f"'{CONF_OPTIONS}' has to list the entries, in the device's own order"
        )
    if not listed and CONF_OPTIONS in config:
        raise cv.Invalid(
            f"'{CONF_OPTIONS}' only applies to a list field; a {config[CONF_VALUE_TYPE]} field "
            f"carries its own text"
        )
    return config


CONFIG_SCHEMA = cv.All(
    text_sensor.text_sensor_schema(MasterbusTextSensor)
    .extend(
        entity_schema(
            value_types=("text", "time", "date", *LIST_VALUE_TYPES),
            default_value_type="text",
        )
    )
    .extend(
        {
            cv.Optional(CONF_OPTIONS): cv.All(
                cv.ensure_list(cv.string_strict), cv.Length(min=1)
            )
        }
    ),
    _validate_options,
)


async def to_code(config: ConfigType) -> None:
    var = await new_entity(text_sensor.new_text_sensor, config)
    if options := config.get(CONF_OPTIONS):
        table = cg.shared_progmem_array(
            "masterbus_text_options", cg.const_char_ptr, options
        )
        cg.add(var.set_options(table, len(options)))
