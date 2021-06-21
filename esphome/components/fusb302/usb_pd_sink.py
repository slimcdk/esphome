import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, usb_pd_sink
from esphome.const import CONF_ID


CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["i2c"]


fusb302_ns = cg.esphome_ns.namespace("fusb302")
FUSB302Component = fusb302_ns.class_(
    "FUSB302Component", usb_pd_sink.UsbPdSink, cg.Component, i2c.I2CDevice
)


CONFIG_SCHEMA = (
    usb_pd_sink.USB_PD_SINK_SCHEMA.extend(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(FUSB302Component),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(None))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)


"""
Example config

i2c:
  sda: GPIO21
  scl: GPIO22


usb_pd_sink:
  - platform: fusb302
    id: fusb302_id
    address: 0x01

"""
