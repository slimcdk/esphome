from esphome.components import stepper, sensor
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID

CONF_STEPPER_ID = "stepper_id"
CONF_ENCODER_ID = "encoder_id"


encoder_ns = cg.esphome_ns.namespace("encoder")
StepperEncoder = encoder_ns.class_("StepperEncoder", stepper.Stepper, cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(StepperEncoder),
        cv.Required(CONF_STEPPER_ID): cv.use_id(stepper.Stepper),
        cv.Required(CONF_ENCODER_ID): cv.use_id(sensor.Sensor),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await stepper.register_stepper(var, config)

    _stepper = await cg.get_variable(config[CONF_STEPPER_ID])
    cg.add(var.set_stepper(_stepper))
    _encoder = await cg.get_variable(config[CONF_ENCODER_ID])
    cg.add(var.set_encoder(_encoder))


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
    stepper: stepper_wo_encoder
    encoder: stepper_encoder_sensor
"""
