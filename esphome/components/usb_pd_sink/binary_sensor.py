import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from . import UsbPdSink, CONF_USB_PD_SINK_ID


CODEOWNERS = ["@slimcdk"]

DEPENDENCIES = ["usb_pd_sink"]

CONF_SORUCE_CONNECTION = "source_connection"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_USB_PD_SINK_ID): cv.use_id(UsbPdSink),
        cv.Optional(
            CONF_SORUCE_CONNECTION
        ): binary_sensor.BINARY_SENSOR_SCHEMA.extend(),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_USB_PD_SINK_ID])
    var = await binary_sensor.new_binary_sensor(config)
    cg.add(hub.set_source_connectior_binary_sensor(var))
