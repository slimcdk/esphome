import esphome.codegen as cg
from esphome.components import text_sensor
from esphome.components.const import CONF_VALUE_TYPE
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_OPTIONS
from esphome.types import ConfigType

from .. import (
    CONF_MASTERBUS_DEVICE_ID,
    MasterbusDevice,
    entity_schema,
    masterbus_ns,
    new_entity,
    register_info_entity,
)

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

CONF_INFO = "info"

MasterbusTextSensor = masterbus_ns.class_(
    "MasterbusTextSensor", text_sensor.TextSensor, cg.PollingComponent
)
MasterbusInfoTextSensor = masterbus_ns.class_(
    "MasterbusInfoTextSensor", text_sensor.TextSensor, cg.Component
)
MasterbusDeviceInfo = masterbus_ns.enum("MasterbusDeviceInfo", is_class=True)

# What a device says about itself. The class needs no question: the device's address carries it.
INFO = {
    "article_number": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_ARTICLE_NUMBER,
    "serial_number": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_SERIAL_NUMBER,
    "product_name": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_PRODUCT_NAME,
    "revision_code": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_REVISION_CODE,
    "firmware": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_FIRMWARE,
    "class": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_CLASS,
}
INFO_CLASS = "class"

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


FIELD_SCHEMA = cv.All(
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

INFO_SCHEMA = (
    text_sensor.text_sensor_schema(MasterbusInfoTextSensor)
    .extend(
        {
            cv.GenerateID(CONF_MASTERBUS_DEVICE_ID): cv.use_id(MasterbusDevice),
            cv.Required(CONF_INFO): cv.enum(INFO, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


def CONFIG_SCHEMA(config: ConfigType) -> ConfigType:
    """A field of a device, by its number, or what the device says about itself, by `info`."""
    if config is cv.SCHEMA_EXTRACT:
        return FIELD_SCHEMA
    if isinstance(config, dict) and CONF_INFO in config:
        return INFO_SCHEMA(config)
    return FIELD_SCHEMA(config)


async def to_code(config: ConfigType) -> None:
    if CONF_INFO in config:
        device = await cg.get_variable(config[CONF_MASTERBUS_DEVICE_ID])
        var = cg.new_Pvariable(config[CONF_ID], device, config[CONF_INFO])
        await cg.register_component(var, config)
        await text_sensor.register_text_sensor(var, config)
        # The sensor exists for every kind of information; only the ones read off the bus take
        # a slot in the hub's list, and the class is not one of them.
        cg.add_define("USE_MASTERBUS_INFO")
        if config[CONF_INFO] != INFO_CLASS:
            await register_info_entity(config[CONF_MASTERBUS_DEVICE_ID], device, var)
        return
    var = await new_entity(text_sensor.new_text_sensor, config)
    if options := config.get(CONF_OPTIONS):
        table = cg.shared_progmem_array(
            "masterbus_text_options", cg.const_char_ptr, options
        )
        cg.add(var.set_options(table, len(options)))
