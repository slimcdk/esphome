from esphome import pins
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, usb_pd_sink
from esphome.const import CONF_ID, CONF_VOLTAGE, CONF_AMPERAGE, CONF_RESET_PIN


CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["i2c"]

CONF_PROFILES = "pdo_profiles"


stusb4500_ns = cg.esphome_ns.namespace("stusb4500")
STUSB4500Component = stusb4500_ns.class_(
    "STUSB4500Component", usb_pd_sink.UsbPdSink, cg.Component, i2c.I2CDevice
)

pdo_profile = {
    cv.Required(CONF_VOLTAGE): cv.int_,
    cv.Required(CONF_AMPERAGE): cv.float_,
}


CONFIG_SCHEMA = (
    usb_pd_sink.USB_PD_SINK_SCHEMA.extend(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(STUSB4500Component),
            cv.Required(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_PROFILES): cv.All(
                cv.ensure_list(pdo_profile), cv.Length(min=1, max=3)
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(None))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    if CONF_PROFILES in config:
        for i, pdo in enumerate(config[CONF_PROFILES], start=1):
            cg.add(var.set_pdo(i, pdo[CONF_VOLTAGE], pdo[CONF_AMPERAGE]))


"""
Example config

esphome:
  on_boot:
    - usb_pd_sink.negotiate:
        id: stusb4500_id
        voltage: 5
        amperage: 5.0

i2c:
  sda: GPIO21
  scl: GPIO22


usb_pd_sink:
  - platform: stusb4500
    id: stusb4500_id
    reset_pin: GPIO4
    address: 0x00
    pdo_profiles:
      - voltage: 5
        amperage: 1
      - voltage: 12
        amperage: 2
      - voltage: 20
        amperage: 3

"""
