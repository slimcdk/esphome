import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_VOLTAGE,
    CONF_AMPERE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_VOLTAGE,
    UNIT_AMPERE,
    UNIT_VOLT,
    ICON_CURRENT_DC,
    ICON_EMPTY,
)
from . import UsbPdSink, CONF_USB_PD_SINK_ID

CODEOWNERS = ["@slimcdk"]

DEPENDENCIES = ["usb_pd_sink"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_USB_PD_SINK_ID): cv.use_id(UsbPdSink),
        cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
            UNIT_VOLT,
            ICON_EMPTY,
            0,
            DEVICE_CLASS_VOLTAGE,
        ).extend(),
        cv.Optional(CONF_AMPERE): sensor.sensor_schema(
            UNIT_AMPERE, ICON_CURRENT_DC, 0, DEVICE_CLASS_CURRENT
        ).extend(),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_USB_PD_SINK_ID])

    if CONF_VOLTAGE in config:
        sens = await sensor.new_sensor(config[CONF_VOLTAGE])
        cg.add(hub.set_voltage_sensor(sens))

    if CONF_AMPERE in config:
        sens = await sensor.new_sensor(config[CONF_AMPERE])
        cg.add(hub.set_ampere_sensor(sens))
