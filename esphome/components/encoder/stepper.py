from esphome.components import stepper, sensor
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID

CONF_STEPPER_ID = "stepper_id"
CONF_ENCODER_ID = "encoder_id"
CONF_RATIO = "ratio"

encoder_ns = cg.esphome_ns.namespace("encoder")
EncoderComponent = encoder_ns.class_("StepperEncoder", stepper.Stepper, cg.Component)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(EncoderComponent),
        cv.Required(CONF_STEPPER_ID): cv.use_id(stepper.Stepper),
        cv.Required(CONF_ENCODER_ID): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_RATIO): cv.templatable(cv.float_),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await stepper.register_stepper(var, config)

    _stepper = await cg.get_variable(config[CONF_STEPPER_ID])
    _encoder = await cg.get_variable(config[CONF_ENCODER_ID])

    cg.add(var.set_stepper(_stepper))
    cg.add(var.set_encoder(_encoder))

    if CONF_RATIO in config:
        cg.add(var.set_ratio_hint(config[CONF_RATIO]))


"""
sensor:
  - platform: rotary_encoder
    id: stepper_encoder_sensor
    pin_a: ...
    pin_b: ...


stepper:
  - platform: ...
    id: stepper_wo_encoder

  - platform: encoder
    stepper_id: stepper_wo_encoder
    encoder_id: stepper_encoder_sensor
"""
