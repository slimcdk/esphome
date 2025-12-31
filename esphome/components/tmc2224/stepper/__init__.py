import esphome.codegen as cg
from esphome.components import stepper
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import EsphomeError

from .. import (
    CONF_DIR_PIN,
    CONF_INDEX_PIN,
    CONF_STEP_PIN,
    CONF_TMC2224_ID,
    TMC2224_BASE_CONFIG_SCHEMA,
    TMC2224Component,
    register_tmc2224_base,
    tmc2224_ns,
    validate_tmc2224_base,
)

CODEOWNERS = ["@slimcdk"]

AUTO_LOAD = ["tmc2224_hub", "tmc2224"]

TMC2224Stepper = tmc2224_ns.class_("TMC2224Stepper", TMC2224Component, stepper.Stepper)
ControlMethod = tmc2224_ns.enum("ControlMethod")


DEVICE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TMC2224_ID): cv.use_id(TMC2224Stepper),
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
            cv.GenerateID(CONF_ID): cv.declare_id(TMC2224Stepper),
        }
    ).extend(TMC2224_BASE_CONFIG_SCHEMA, stepper.STEPPER_SCHEMA),
    cv.has_none_or_all_keys(CONF_STEP_PIN, CONF_DIR_PIN),
    validate_control_method_,
    validate_tmc2224_base,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await register_tmc2224_base(var, config)
    await stepper.register_stepper(var, config)

    has_index_pin = CONF_INDEX_PIN in config
    has_stepdir_pins = CONF_STEP_PIN in config and CONF_DIR_PIN in config

    if has_index_pin:
        cg.add(var.set_control_method(ControlMethod.SERIAL_CONTROL))
    elif has_stepdir_pins:
        cg.add(var.set_control_method(ControlMethod.PULSES_CONTROL))
    else:
        raise EsphomeError("Could not determine control method!")
