import logging

from esphome import automation, pins
from esphome.automation import maybe_simple_id
import esphome.codegen as cg
from esphome.components import stepper, tmc_hub
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_DIR_PIN,
    CONF_DIRECTION,
    CONF_ID,
    CONF_STEP_PIN,
    CONF_THRESHOLD,
    CONF_TRIGGER_ID,
)
from esphome.core import EsphomeError

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@slimcdk"]

AUTO_LOAD = ["tmc_hub"]

CONF_TMC = "tmc"
CONF_TMC_ID = "tmc_id"

CONF_ENN_PIN = "enn_pin"
CONF_DIAG_PIN = "diag_pin"
CONF_INDEX_PIN = "index_pin"
CONF_SELECT_PIN = "select_pin"

CONF_CLOCK_FREQUENCY = "clock_frequency"
CONF_OTTRIM = "ottrim"
CONF_VSENSE = "vsense"  # true lowers power dissipation in sense resistors
CONF_RSENSE = "rsense"  # sense resistors
CONF_ANALOG_CURRENT_SCALE = "analog_current_scale"
CONF_INCLUDE_REGISTERS = "config_dump_include_registers"
CONF_ON_DRIVER_STATUS = "on_status"
CONF_ON_STALL = "on_stall"

# used in actions
CONF_MICROSTEPS = "microsteps"  # CHOPCONF.mres
CONF_INTERPOLATION = "interpolation"  # CHOPCONF.intpol
CONF_IRUN = "irun"
CONF_RUN_CURRENT = "run_current"  # translates to IRUN
CONF_IHOLD = "ihold"
CONF_HOLD_CURRENT = "hold_current"  # translates to IHOLD
CONF_IHOLDDELAY = "iholddelay"
CONF_TPOWERDOWN = "tpowerdown"
CONF_ENABLE_SPREADCYCLE = "enable_spreadcycle"
CONF_TCOOL_THRESHOLD = "tcool_threshold"
CONF_TPWM_THRESHOLD = "tpwm_threshold"
CONF_STANDSTILL_MODE = "standstill_mode"
CONF_SEIMIN = "seimin"
CONF_SEDN = "sedn"
CONF_SEMAX = "semax"
CONF_SEUP = "seup"
CONF_SEMIN = "semin"
CONF_TBL = "tbl"
CONF_HEND = "hend"
CONF_HSTRT = "hstrt"
CONF_PWM_LIM = "lim"
CONF_PWM_REG = "reg"
CONF_PWM_AUTOGRAD = "autograd"
CONF_PWM_AUTOSCALE = "autoscale"
CONF_PWM_FREQ = "freq"
CONF_PWM_GRAD = "grad"
CONF_PWM_OFS = "ofs"
CONF_RESTORE_TOFF = "restore_toff"


TYPE_TMC2100 = "tmc2100"  # 0x10
TYPE_TMC2130 = "tmc2130"  # 0x10
TYPE_TMC5130 = "tmc5130"  # 0x11
TYPE_TMC5130A = "tmc5130a"  # 0x11
TYPE_TMC5031 = "tmc5031"  # 0x12
TYPE_TMC5041 = "tmc5041"  # 0x13
TYPE_TMC2202 = "tmc2202"  # 0x20
TYPE_TMC2208 = "tmc2208"  # 0x20
TYPE_TMC2224 = "tmc2224"  # 0x20
TYPE_TMC2225 = "tmc2225"  # 0x20
TYPE_TMC2209 = "tmc2209"  # 0x21
TYPE_TMC2226 = "tmc2226"  # 0x21
TYPE_TMC2210 = "tmc2210"  # 0x24
TYPE_TMC2160 = "tmc2160"  # 0x30
TYPE_TMC5160 = "tmc5160"  # 0x30
TYPE_TMC5161 = "tmc5161"  # 0x31
TYPE_TMC5160A = "tmc5160a"  # 0x32
TYPE_TMC5160PRO = "tmc5160pro"  # 0x32
TYPE_TMC2240 = "tmc2240"  # 0x40
TYPE_TMC2241 = "tmc2241"  # 0x40
TYPE_TMC5240 = "tmc5240"  # 0x40
TYPE_TMC5241 = "tmc5241"  # 0x40
TYPE_TMC2590 = "tmc2590"  # 0x50
TYPE_TMC260 = "tmc260"  # 0x60
TYPE_TMC261 = "tmc261"  # 0x60
TYPE_TMC262 = "tmc262"  # 0x60
TYPE_TMC2660 = "tmc2660"  # 0x60


tmc_ns = cg.esphome_ns.namespace("tmc")

TMCAPI = tmc_ns.class_("TMCAPI", cg.Parented.template(tmc_hub.TMCHub))
TMCComponent = tmc_ns.class_("TMCComponent", TMCAPI, cg.Component)
TMCStepper = tmc_ns.class_("TMCStepper", TMCComponent, stepper.Stepper)

MODELS = {
    # TYPE_TMC2100: tmc_ns.class_("TMC2100", TMCStepper),        # 0x10
    # TYPE_TMC2130: tmc_ns.class_("TMC2130", TMCStepper),        # 0x10
    # TYPE_TMC5130: tmc_ns.class_("TMC5130", TMCStepper),        # 0x11
    # TYPE_TMC5130A: tmc_ns.class_("TMC5130A", TMCStepper),      # 0x11
    # TYPE_TMC5031: tmc_ns.class_("TMC5031", TMCStepper),        # 0x12
    # TYPE_TMC5041: tmc_ns.class_("TMC5041", TMCStepper),        # 0x13
    TYPE_TMC2202: tmc_ns.class_("TMC0X20", TMCStepper),  # 0x20
    TYPE_TMC2208: tmc_ns.class_("TMC0X20", TMCStepper),  # 0x20
    TYPE_TMC2224: tmc_ns.class_("TMC0X20", TMCStepper),  # 0x20
    TYPE_TMC2225: tmc_ns.class_("TMC0X20", TMCStepper),  # 0x20
    TYPE_TMC2209: tmc_ns.class_("TMC0X21", TMCStepper),  # 0x21
    TYPE_TMC2226: tmc_ns.class_("TMC0X21", TMCStepper),  # 0x21
    # TYPE_TMC2210: tmc_ns.class_("TMC2210", TMCStepper),        # 0x24
    # TYPE_TMC2160: tmc_ns.class_("TMC2160", TMCStepper),        # 0x30
    # TYPE_TMC5160: tmc_ns.class_("TMC5160", TMCStepper),        # 0x30
    # TYPE_TMC5161: tmc_ns.class_("TMC5161", TMCStepper),        # 0x31
    # TYPE_TMC5160A: tmc_ns.class_("TMC5160A", TMCStepper),      # 0x32
    # TYPE_TMC5160PRO: tmc_ns.class_("TMC5160PRO", TMCStepper),  # 0x32
    # TYPE_TMC2240: tmc_ns.class_("TMC2240", TMCStepper),        # 0x40
    # TYPE_TMC2241: tmc_ns.class_("TMC2241", TMCStepper),        # 0x40
    # TYPE_TMC5240: tmc_ns.class_("TMC5240", TMCStepper),        # 0x40
    # TYPE_TMC5241: tmc_ns.class_("TMC5241", TMCStepper),        # 0x40
    # TYPE_TMC2590: tmc_ns.class_("TMC2590", TMCStepper),        # 0x50
    # TYPE_TMC260: tmc_ns.class_("TMC260", TMCStepper),          # 0x60
    # TYPE_TMC261: tmc_ns.class_("TMC261", TMCStepper),          # 0x60
    # TYPE_TMC262: tmc_ns.class_("TMC262", TMCStepper),          # 0x60
    # TYPE_TMC2660: tmc_ns.class_("TMC2660", TMCStepper),        # 0x60
}


ControlMethod = tmc_ns.enum("ControlMethod")
DriverStatusEvent = tmc_ns.enum("DriverStatusEvent")
StandstillMode = tmc_ns.enum("StandstillMode")
ShaftDirection = tmc_ns.enum("ShaftDirection")

OnDriverStatusTrigger = tmc_ns.class_("OnDriverStatusTrigger", automation.Trigger)
OnStallTrigger = tmc_ns.class_("OnStallTrigger", automation.Trigger)

ConfigureAction = tmc_ns.class_("ConfigureAction", automation.Action)
ActivationAction = tmc_ns.class_("ActivationAction", automation.Action)
CurrentsAction = tmc_ns.class_("CurrentsAction", automation.Action)
StallGuardAction = tmc_ns.class_("StallGuardAction", automation.Action)
CoolConfAction = tmc_ns.class_("CoolConfAction", automation.Action)
ChopConfAction = tmc_ns.class_("ChopConfAction", automation.Action)
PWMConfAction = tmc_ns.class_("PWMConfAction", automation.Action)

# SyncAction = tmc_ns.class_("SyncAction", automation.Action)


STANDSTILL_MODES = {
    "normal": StandstillMode.NORMAL,
    "freewheeling": StandstillMode.FREEWHEELING,
    "coil_short_ls": StandstillMode.COIL_SHORT_LS,
    "coil_short_hs": StandstillMode.COIL_SHORT_HS,
}

SHAFT_DIRECTIONS = {
    "clockwise": ShaftDirection.CLOCKWISE,
    "cw": ShaftDirection.CLOCKWISE,
    "counterclockwise": ShaftDirection.COUNTERCLOCKWISE,
    "ccw": ShaftDirection.COUNTERCLOCKWISE,
}

DEVICE_SCHEMA = cv.Schema({cv.GenerateID(CONF_TMC_ID): cv.use_id(TMCComponent)})


# CONFIG_SCHEMA = cv.All(
#     cv.Schema(
#         {
#             cv.GenerateID(CONF_ID): cv.declare_id(TMCStepper),
#         }
#     ).extend(TMC2209_BASE_CONFIG_SCHEMA, stepper.STEPPER_SCHEMA),
#     cv.has_none_or_all_keys(CONF_STEP_PIN, CONF_DIR_PIN),
#     validate_control_method_,
#     validate_tmc2209_base,
# )

# BASE_CONFIG_SCHEMA = cv.Schema(
#     {
#         cv.GenerateID(CONF_ID): cv.declare_id(tmc_ns.class_("TMC0X21", TMCStepper)),
#         cv.Optional(CONF_ENN_PIN): pins.internal_gpio_output_pin_schema,
#         cv.Optional(CONF_DIAG_PIN): pins.internal_gpio_input_pin_schema,
#         cv.Optional(CONF_INDEX_PIN): pins.internal_gpio_input_pin_schema,
#         cv.Optional(CONF_STEP_PIN): pins.gpio_output_pin_schema,
#         cv.Optional(CONF_DIR_PIN): pins.gpio_output_pin_schema,
#         # cv.Optional(CONF_SELECT_PIN): pins.gpio_output_pin_schema,
#         cv.Optional(CONF_VSENSE): cv.boolean,  # default OTP
#         cv.Optional(CONF_OTTRIM): cv.int_range(0, 3),  # default OTP
#         cv.Optional(CONF_RSENSE): cv.resistance,  # default is rdson
#         cv.Optional(CONF_ANALOG_CURRENT_SCALE, default=False): cv.boolean,
#         cv.Optional(CONF_CLOCK_FREQUENCY, default=12_000_000): cv.All(
#             cv.positive_int, cv.frequency
#         ),
#         cv.Optional(CONF_ON_DRIVER_STATUS): automation.validate_automation(
#             {
#                 cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnDriverStatusTrigger),
#             }
#         ),
#         cv.Optional(CONF_INCLUDE_REGISTERS, default=False): cv.boolean,
#     },
# ).extend(cv.COMPONENT_SCHEMA, stepper.STEPPER_SCHEMA, tmc_hub.TMC_HUB_DEVICE_SCHEMA)


def config_schema(model_type):
    return cv.Schema(
        {
            # Declare the ID using the specific class for this model
            cv.GenerateID(CONF_ID): cv.declare_id(MODELS[model_type]),
            cv.Optional(CONF_ENN_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_DIAG_PIN): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_INDEX_PIN): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_STEP_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_DIR_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_VSENSE): cv.boolean,
            cv.Optional(CONF_OTTRIM): cv.int_range(0, 3),
            cv.Optional(CONF_RSENSE): cv.resistance,
            cv.Optional(CONF_ANALOG_CURRENT_SCALE, default=False): cv.boolean,
            cv.Optional(CONF_CLOCK_FREQUENCY, default=12_000_000): cv.All(
                cv.positive_int, cv.frequency
            ),
            cv.Optional(CONF_ON_DRIVER_STATUS): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        OnDriverStatusTrigger
                    ),
                }
            ),
            cv.Optional(CONF_INCLUDE_REGISTERS, default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA, stepper.STEPPER_SCHEMA, tmc_hub.TMC_HUB_DEVICE_SCHEMA)


def _0x20_config_schema(model_type):
    return config_schema(model_type)


def _0x21_config_schema(model_type):
    return config_schema(model_type).extend(
        {
            cv.Optional(CONF_ADDRESS, default=0x00): cv.hex_uint8_t,
            cv.Optional(CONF_ON_STALL): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnStallTrigger),
                }
            ),
        }
    )


CONFIG_SCHEMA = cv.typed_schema(
    {
        # 0x20
        TYPE_TMC2202: _0x20_config_schema(TYPE_TMC2202),
        TYPE_TMC2208: _0x20_config_schema(TYPE_TMC2208),
        TYPE_TMC2224: _0x20_config_schema(TYPE_TMC2224),
        TYPE_TMC2225: _0x20_config_schema(TYPE_TMC2225),
        # 0x21
        TYPE_TMC2209: _0x21_config_schema(TYPE_TMC2209),
        TYPE_TMC2226: _0x21_config_schema(TYPE_TMC2226),
    },
    key="model",
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
    await stepper.register_stepper(var, config)

    # if tmc_hub.CONF_TMC_HUB_ID in config:
    await register_uart_tmc(var, config)

    cg.add(var.set_model(f"tmc.{config['model']}"))
    cg.add(var.set_clk_freq(config[CONF_CLOCK_FREQUENCY]))
    cg.add(var.set_analog_current_scale(config[CONF_ANALOG_CURRENT_SCALE]))
    cg.add(var.set_config_dump_include_registers(config[CONF_INCLUDE_REGISTERS]))

    if (enn_pin := config.get(CONF_ENN_PIN, None)) is not None:
        cg.add(var.set_enn_pin(await cg.gpio_pin_expression(enn_pin)))

    if (diag_pin := config.get(CONF_DIAG_PIN, None)) is not None:
        cg.add(var.set_diag_pin(await cg.gpio_pin_expression(diag_pin)))

    if (index_pin := config.get(CONF_INDEX_PIN, None)) is not None:
        cg.add(var.set_index_pin(await cg.gpio_pin_expression(index_pin)))

    if (select_pin := config.get(CONF_SELECT_PIN, None)) is not None:
        cg.add(var.set_select_pin(await cg.gpio_pin_expression(select_pin)))

    if (step_pin := config.get(CONF_STEP_PIN, None)) is not None:
        cg.add(var.set_step_pin(await cg.gpio_pin_expression(step_pin)))

    if (dir_pin := config.get(CONF_DIR_PIN, None)) is not None:
        cg.add(var.set_dir_pin(await cg.gpio_pin_expression(dir_pin)))

    has_index_pin = CONF_INDEX_PIN in config
    has_stepdir_pins = CONF_STEP_PIN in config and CONF_DIR_PIN in config

    if has_index_pin:
        cg.add(var.set_control_method(ControlMethod.SERIAL_CONTROL))
    elif has_stepdir_pins:
        cg.add(var.set_control_method(ControlMethod.PULSES_CONTROL))
    else:
        raise EsphomeError("Could not determine control method!")

    if (rsense := config.get(CONF_RSENSE, None)) is not None:
        cg.add(var.set_rsense(rsense))

    if (vsense := config.get(CONF_VSENSE, None)) is not None:
        cg.add(var.set_vsense(vsense))
        if CONF_RSENSE not in config and vsense is False:
            _LOGGER.warning(
                "High heat dissipation (`vsense: False`) when using RDSon / internal current sensing",
            )

    if (ottrim := config.get(CONF_OTTRIM, None)) is not None:
        cg.add(var.set_ottrim(ottrim))

    for conf in config.get(CONF_ON_DRIVER_STATUS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(DriverStatusEvent, "code")], conf)
        cg.add(var.set_enable_driver_health_check(True))

    for conf in config.get(CONF_ON_STALL, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
        cg.add(var.set_enable_stall_detection(True))

    cg.add_build_flag("-std=c++17")
    cg.add_build_flag("-std=gnu++17")

    return var


async def register_uart_tmc(var, config):
    await tmc_hub.register_tmc_hub_device(var, config)
    if (address := config.get(CONF_ADDRESS, None)) is not None:
        cg.add(var.set_address(address))


def validate_control_method_(config):
    has_index_pin = CONF_INDEX_PIN in config
    has_stepdir_pins = CONF_STEP_PIN in config and CONF_DIR_PIN in config

    if not has_index_pin and not has_stepdir_pins:
        raise cv.Invalid(
            f"Either {CONF_INDEX_PIN} and/or {CONF_STEP_PIN} and {CONF_DIR_PIN} must be configured"
        )
    return config


@automation.register_action(
    "tmc.enable",
    ActivationAction,
    maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(TMCComponent),
            cv.Optional(CONF_RESTORE_TOFF, default=True): cv.boolean,
        }
    ),
)
async def tmc_enable_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_activate(True))
    cg.add(var.set_toff_recovery(config[CONF_RESTORE_TOFF]))
    return var


@automation.register_action(
    "tmc.disable",
    ActivationAction,
    maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(TMCComponent),
            cv.Optional(CONF_RESTORE_TOFF, default=True): cv.boolean,
        }
    ),
)
async def tmc_disable_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_activate(False))
    cg.add(var.set_toff_recovery(config[CONF_RESTORE_TOFF]))
    return var


@automation.register_action(
    "tmc.configure",
    ConfigureAction,
    maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(TMCComponent),
            cv.Optional(CONF_DIRECTION): cv.templatable(cv.enum(SHAFT_DIRECTIONS)),
            cv.Optional(CONF_MICROSTEPS): cv.templatable(
                cv.one_of(256, 128, 64, 32, 16, 8, 4, 2, 1, int=True)
            ),
            cv.Optional(CONF_INTERPOLATION): cv.templatable(cv.boolean),
            cv.Optional(CONF_ENABLE_SPREADCYCLE): cv.templatable(cv.boolean),
            cv.Optional(CONF_TCOOL_THRESHOLD): cv.templatable(
                cv.int_range(min=0, max=2**20, max_included=False)
            ),
            cv.Optional(CONF_TPWM_THRESHOLD): cv.templatable(
                cv.int_range(min=0, max=2**20, max_included=False)
            ),
        }
    ),
)
async def tmc_configure_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    if (dir := config.get(CONF_DIRECTION, None)) is not None:
        template_ = await cg.templatable(dir, args, ShaftDirection)
        cg.add(var.set_inverse_direction(template_))

    if (microsteps := config.get(CONF_MICROSTEPS, None)) is not None:
        template_ = await cg.templatable(microsteps, args, cg.int16)
        cg.add(var.set_microsteps(template_))

    if (interpolation := config.get(CONF_INTERPOLATION, None)) is not None:
        template_ = await cg.templatable(interpolation, args, cg.bool_)
        cg.add(var.set_microstep_interpolation(template_))

    if (en_spreadcycle := config.get(CONF_ENABLE_SPREADCYCLE, None)) is not None:
        template_ = await cg.templatable(en_spreadcycle, args, cg.bool_)
        cg.add(var.set_enable_spreadcycle(template_))

    if (tcoolthrs := config.get(CONF_TCOOL_THRESHOLD, None)) is not None:
        template_ = await cg.templatable(tcoolthrs, args, cg.uint32)
        cg.add(var.set_tcool_threshold(template_))

    if (tpwmthrs := config.get(CONF_TPWM_THRESHOLD, None)) is not None:
        template_ = await cg.templatable(tpwmthrs, args, cg.uint32)
        cg.add(var.set_tpwm_threshold(template_))

    return var


@automation.register_action(
    "tmc.currents",
    CurrentsAction,
    maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(TMCComponent),
            cv.Exclusive(CONF_RUN_CURRENT, "run current"): cv.templatable(
                cv.All(cv.current, cv.positive_not_null_float)
            ),
            cv.Exclusive(CONF_IRUN, "run current"): cv.templatable(
                cv.int_range(min=0, max=31)
            ),
            cv.Exclusive(CONF_HOLD_CURRENT, "hold current"): cv.templatable(
                cv.All(cv.current, cv.positive_float)
            ),
            cv.Exclusive(CONF_IHOLD, "hold current"): cv.templatable(
                cv.int_range(min=0, max=31)
            ),
            cv.Optional(CONF_STANDSTILL_MODE): cv.templatable(
                cv.enum(STANDSTILL_MODES, string=True)
            ),
            cv.Optional(CONF_IHOLDDELAY): cv.templatable(cv.int_range(0, 15)),
            cv.Optional(CONF_TPOWERDOWN): cv.templatable(cv.int_range(0, 255)),
        }
    ),
)
async def tmc_currents_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    if (standstill_mode := config.get(CONF_STANDSTILL_MODE, None)) is not None:
        template_ = await cg.templatable(standstill_mode, args, StandstillMode)
        cg.add(var.set_standstill_mode(template_))

    if (run_current := config.get(CONF_RUN_CURRENT, None)) is not None:
        template_ = await cg.templatable(run_current, args, cg.float_)
        cg.add(var.set_run_current(template_))

    if (irun := config.get(CONF_IRUN, None)) is not None:
        template_ = await cg.templatable(irun, args, cg.uint8)
        cg.add(var.set_irun(template_))

    if (hold_current := config.get(CONF_HOLD_CURRENT, None)) is not None:
        template_ = await cg.templatable(hold_current, args, cg.float_)
        cg.add(var.set_hold_current(template_))

    if (ihold := config.get(CONF_IHOLD, None)) is not None:
        template_ = await cg.templatable(ihold, args, cg.uint8)
        cg.add(var.set_ihold(template_))

    if (iholddelay := config.get(CONF_IHOLDDELAY, None)) is not None:
        template_ = await cg.templatable(iholddelay, args, cg.uint8)
        cg.add(var.set_iholddelay(template_))

    if (tpowerdown := config.get(CONF_TPOWERDOWN, None)) is not None:
        template_ = await cg.templatable(tpowerdown, args, cg.uint8)
        cg.add(var.set_tpowerdown(template_))

    return var


@automation.register_action(
    "tmc.stallguard",
    StallGuardAction,
    maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(TMCComponent),
            cv.Optional(CONF_THRESHOLD): cv.templatable(
                cv.int_range(min=0, max=2**8, max_included=False)
            ),
        }
    ),
)
async def tmc_stallguard_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    if (sgthrs := config.get(CONF_THRESHOLD, None)) is not None:
        template_ = await cg.templatable(sgthrs, args, cg.uint8)
        cg.add(var.set_stallguard_threshold(template_))

    return var


@automation.register_action(
    "tmc.coolconf",
    CoolConfAction,
    maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(TMCComponent),
            cv.Optional(CONF_SEIMIN): cv.templatable(
                cv.All(cv.boolean, cv.int_range(min=0, max=1))
            ),
            cv.Optional(CONF_SEMAX): cv.templatable(
                cv.int_range(min=0, max=2**4, max_included=False)
            ),
            cv.Optional(CONF_SEMIN): cv.templatable(
                cv.int_range(min=0, max=2**4, max_included=False)
            ),
            cv.Optional(CONF_SEDN): cv.templatable(
                cv.int_range(min=0, max=2**2, max_included=False)
            ),
            cv.Optional(CONF_SEUP): cv.templatable(
                cv.int_range(min=0, max=2**2, max_included=False)
            ),
        }
    ),
)
async def tmc_coolconf_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    if (seimin := config.get(CONF_SEIMIN, None)) is not None:
        template_ = await cg.templatable(seimin, args, cg.uint8)
        cg.add(var.set_seimin(template_))

    if (semax := config.get(CONF_SEMAX, None)) is not None:
        template_ = await cg.templatable(semax, args, cg.uint8)
        cg.add(var.set_semax(template_))

    if (semin := config.get(CONF_SEMIN, None)) is not None:
        template_ = await cg.templatable(semin, args, cg.uint8)
        cg.add(var.set_semin(template_))

    if (sedn := config.get(CONF_SEDN, None)) is not None:
        template_ = await cg.templatable(sedn, args, cg.uint8)
        cg.add(var.set_sedn(template_))

    if (seup := config.get(CONF_SEUP, None)) is not None:
        template_ = await cg.templatable(seup, args, cg.uint8)
        cg.add(var.set_seup(template_))

    return var


@automation.register_action(
    "tmc.chopconf",
    ChopConfAction,
    maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(TMCComponent),
            cv.Optional(CONF_TBL): cv.templatable(
                cv.int_range(min=0, max=2**2, max_included=False)
            ),
            cv.Optional(CONF_HEND): cv.templatable(
                cv.int_range(min=0, max=2**4, max_included=False)
            ),
            cv.Optional(CONF_HSTRT): cv.templatable(
                cv.int_range(min=0, max=2**3, max_included=False)
            ),
        }
    ),
)
async def tmc_chopconf_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    if (tbl := config.get(CONF_TBL, None)) is not None:
        template_ = await cg.templatable(tbl, args, cg.uint8)
        cg.add(var.set_tbl(template_))

    if (hend := config.get(CONF_HEND, None)) is not None:
        template_ = await cg.templatable(hend, args, cg.uint8)
        cg.add(var.set_hend(template_))

    if (hstrt := config.get(CONF_HSTRT, None)) is not None:
        template_ = await cg.templatable(hstrt, args, cg.uint8)
        cg.add(var.set_hstrt(template_))

    return var


@automation.register_action(
    "tmc.pwmconf",
    PWMConfAction,
    maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(TMCComponent),
            cv.Optional(CONF_PWM_LIM): cv.templatable(
                cv.int_range(min=0, max=2**4, max_included=False)
            ),
            cv.Optional(CONF_PWM_REG): cv.templatable(
                cv.int_range(min=0, max=2**4, max_included=False)
            ),
            cv.Optional(CONF_PWM_FREQ): cv.templatable(
                cv.int_range(min=0, max=2**2, max_included=False)
            ),
            cv.Optional(CONF_PWM_GRAD): cv.templatable(
                cv.int_range(min=0, max=2**8, max_included=False)
            ),
            cv.Optional(CONF_PWM_OFS): cv.templatable(
                cv.int_range(min=0, max=2**8, max_included=False)
            ),
            cv.Optional(CONF_PWM_AUTOGRAD): cv.templatable(cv.boolean),
            cv.Optional(CONF_PWM_AUTOSCALE): cv.templatable(cv.boolean),
        }
    ),
)
async def tmc_pwmconf_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    if (pwmlim := config.get(CONF_PWM_LIM, None)) is not None:
        template_ = await cg.templatable(pwmlim, args, cg.uint8)
        cg.add(var.set_pwmlim(template_))

    if (pwmreg := config.get(CONF_PWM_REG, None)) is not None:
        template_ = await cg.templatable(pwmreg, args, cg.uint8)
        cg.add(var.set_pwmreg(template_))

    if (pwmfreq := config.get(CONF_PWM_FREQ, None)) is not None:
        template_ = await cg.templatable(pwmfreq, args, cg.uint8)
        cg.add(var.set_pwmfreq(template_))

    if (pwmgrad := config.get(CONF_PWM_GRAD, None)) is not None:
        template_ = await cg.templatable(pwmgrad, args, cg.uint8)
        cg.add(var.set_pwmgrad(template_))

    if (pwmofs := config.get(CONF_PWM_OFS, None)) is not None:
        template_ = await cg.templatable(pwmofs, args, cg.uint8)
        cg.add(var.set_pwmofs(template_))

    if (pwmautograd := config.get(CONF_PWM_AUTOGRAD, None)) is not None:
        template_ = await cg.templatable(pwmautograd, args, cg.bool_)
        cg.add(var.set_pwmautograd(template_))

    if (pwmautoscale := config.get(CONF_PWM_AUTOSCALE, None)) is not None:
        template_ = await cg.templatable(pwmautoscale, args, cg.bool_)
        cg.add(var.set_pwmautoscale(template_))

    return var


# @automation.register_action(
#     "tmc.sync",
#     SyncAction,
#     cv.Schema(
#         {
#             cv.GenerateID(): cv.use_id(TMCComponent),
#             cv.Required(CONF_TO): cv.All(
#                 cv.ensure_list(cv.use_id(TMCComponent)), cv.Length(min=1)
#             ),
#         }
#     ),
# )
# async def tmc_sync_to_code(config, action_id, template_arg, args):
#     var = cg.new_Pvariable(action_id, template_arg)
#     await cg.register_parented(var, config[CONF_ID])

#     if config[CONF_ID] in config[CONF_TO]:
#         _LOGGER.error("tmc.sync for %s is syncing to self", config[CONF_ID])

#     if len(config[CONF_TO]) != len(set(config[CONF_TO])):
#         _LOGGER.warning("tmc.sync for %s has duplicate references", config[CONF_ID])

#     template_ = await cg.templatable(
#         [await cg.get_variable(id) for id in config[CONF_TO]],
#         args,
#         cg.std_vector.template(TMCComponent),
#     )
#     cg.add(var.set_drivers(template_))

#     return var
