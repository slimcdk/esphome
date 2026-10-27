import esphome.codegen as cg
from esphome.components import text_sensor
from esphome.components.const import CONF_VALUE_TYPE
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_OPTIONS, CONF_TYPE
from esphome.types import ConfigType

from .. import (
    CONF_MASTERBUS_DEVICE_ID,
    LIST_VALUE_TYPES,
    MasterbusDevice,
    MasterbusEntity,
    entity_schema,
    masterbus_ns,
    new_entity,
    option_names,
    raw_options,
    register_info_sensor,
    set_device_options,
    validate_options,
)

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusTextSensor = masterbus_ns.class_(
    "MasterbusTextSensor", text_sensor.TextSensor, MasterbusEntity
)
MasterbusClassTextSensor = masterbus_ns.class_(
    "MasterbusClassTextSensor", text_sensor.TextSensor, cg.Component
)
MasterbusDeviceInfo = masterbus_ns.enum("MasterbusDeviceInfo", is_class=True)

# A field of the device, by its number, is the type a text sensor has unless it names another.
TYPE_FIELD = "field"
# What kind of equipment the device is, which its address carries.
TYPE_CLASS = "class"
# What a device says about itself, read off the bus.
INFO = {
    "article_number": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_ARTICLE_NUMBER,
    "serial_number": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_SERIAL_NUMBER,
    "product_name": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_PRODUCT_NAME,
    "revision_code": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_REVISION_CODE,
    "firmware": MasterbusDeviceInfo.MASTERBUS_DEVICE_INFO_FIRMWARE,
}


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
    .extend({cv.Optional(CONF_OPTIONS): raw_options}),
    _validate_options,
    validate_options,
)

_DEVICE_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_MASTERBUS_DEVICE_ID): cv.use_id(MasterbusDevice)}
)
INFO_SCHEMA = text_sensor.text_sensor_schema().extend(_DEVICE_SCHEMA)
CLASS_SCHEMA = (
    text_sensor.text_sensor_schema(MasterbusClassTextSensor)
    .extend(_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

CONFIG_SCHEMA = cv.typed_schema(
    {
        TYPE_FIELD: FIELD_SCHEMA,
        TYPE_CLASS: CLASS_SCHEMA,
        **dict.fromkeys(INFO, INFO_SCHEMA),
    },
    key=CONF_TYPE,
    default_type=TYPE_FIELD,
    lower=True,
)


async def to_code(config: ConfigType) -> None:
    if (kind := config[CONF_TYPE]) == TYPE_CLASS:
        # The class needs no read, so it takes no slot in the hub's list.
        device = await cg.get_variable(config[CONF_MASTERBUS_DEVICE_ID])
        var = cg.new_Pvariable(config[CONF_ID], device)
        await cg.register_component(var, config)
        await text_sensor.register_text_sensor(var, config)
        cg.add_define("USE_MASTERBUS_DEVICE_CLASS")
        return
    if kind != TYPE_FIELD:
        device = await cg.get_variable(config[CONF_MASTERBUS_DEVICE_ID])
        var = await text_sensor.new_text_sensor(config)
        await register_info_sensor(
            config[CONF_MASTERBUS_DEVICE_ID], device, var, INFO[kind]
        )
        return
    var = await new_entity(text_sensor.new_text_sensor, config)
    if CONF_OPTIONS in config:
        options = option_names(config)
        table = cg.shared_progmem_array(
            "masterbus_text_options", cg.const_char_ptr, options
        )
        cg.add(var.set_options(table, len(options)))
        await set_device_options(var, config)
