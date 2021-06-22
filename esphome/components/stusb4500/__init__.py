import esphome.codegen as cg
from esphome.components import i2c, usb_pd_sink


CODEOWNERS = ["@slimcdk"]

CONF_STUSB4500_ID = "stusb4500_id"

stusb4500_ns = cg.esphome_ns.namespace("stusb4500")
STUSB4500Component = stusb4500_ns.class_(
    "STUSB4500Component", usb_pd_sink.UsbPdSink, cg.Component, i2c.I2CDevice
)
