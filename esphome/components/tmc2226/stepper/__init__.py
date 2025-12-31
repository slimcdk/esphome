import esphome.codegen as cg
from esphome.components import stepper
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import EsphomeError

from .. import (
    CONF_DIR_PIN,
    CONF_INDEX_PIN,
    CONF_STEP_PIN,
    CONF_TMC2226_ID,
    TMC2226_BASE_CONFIG_SCHEMA,
    TMC2226Component,
    register_tmc2226_base,
    tmc2226_ns,
    validate_tmc2226_base,
)

CODEOWNERS = ["@slimcdk"]

AUTO_LOAD = ["tmc2226_hub", "tmc2226"]

TMC2226Stepper = tmc2226_ns.class_("TMC2226Stepper", TMC2226Component, stepper.Stepper)
ControlMethod = tmc2226_ns.enum("ControlMethod")


DEVICE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TMC2226_ID): cv.use_id(TMC2226Stepper),
    }
)


def validate_control_method_(config):
    has_index_pin = CONF_INDEX_PIN in config
    has_stepdir_pins = CONF_STEP_PIN in config and CONF_DIR_PIN in config

    if not has_index_pin and not has_stepdir_pins:
        raise cv.Invalid(
            f"Either {CONF_INDEX_PIN} and/or {CONF_STEP_PIN} and {CONF_DIR_PIN} must be configured"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(TMC2226Stepper),
        }
    ).extend(TMC2226_BASE_CONFIG_SCHEMA, stepper.STEPPER_SCHEMA),
    cv.has_none_or_all_keys(CONF_STEP_PIN, CONF_DIR_PIN),
    validate_control_method_,
    validate_tmc2226_base,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await register_tmc2226_base(var, config)
    await stepper.register_stepper(var, config)

    has_index_pin = CONF_INDEX_PIN in config
    has_stepdir_pins = CONF_STEP_PIN in config and CONF_DIR_PIN in config

    if has_index_pin:
        cg.add(var.set_control_method(ControlMethod.SERIAL_CONTROL))
    elif has_stepdir_pins:
        cg.add(var.set_control_method(ControlMethod.PULSES_CONTROL))
    else:
        raise EsphomeError("Could not determine control method!")
