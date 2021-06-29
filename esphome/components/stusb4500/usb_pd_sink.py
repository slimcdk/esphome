from esphome import pins
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, usb_pd_sink
from esphome.const import CONF_ID, CONF_RESET_PIN
from . import STUSB4500Component


CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["i2c"]

CONF_PROFILES = "pdo_profiles"
CONF_ALERT_PIN = "alert_pin"


CONFIG_SCHEMA = (
    usb_pd_sink.PD_SINK_SCHEMA.extend(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(STUSB4500Component),
            cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_ALERT_PIN): pins.gpio_input_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x28))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    await usb_pd_sink.register_usb_pd_sink(var, config)

    if CONF_RESET_PIN in config:
        reset_pin = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(reset_pin))

    if CONF_ALERT_PIN in config:
        alert_pin = await cg.gpio_pin_expression(config[CONF_ALERT_PIN])
        cg.add(var.set_alert_pin(alert_pin))

    cg.add_library("sparkfun/SparkFun STUSB4500", "1.1.4")
