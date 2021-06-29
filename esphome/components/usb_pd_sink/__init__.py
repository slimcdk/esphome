import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_VOLTAGE, CONF_AMPERE, CONF_TRIGGER_ID
from esphome.core import CORE, coroutine_with_priority

# Useful links
# General knowledge:    https://www.digikey.com/en/articles/designing-in-usb-type-c-and-using-power-delivery-for-rapid-charging
#                       https://medium.com/@kolluru.nathan/usb-pd-power-reserve-and-you-71cf4d18505c
# USB PD Power Rules:   https://www.chromium.org/chromium-os/cable-and-adapter-tips-and-tricks#:~:text=DCP%20or%20CDP.-,USB%20PD%20Power%20Rules,-Power%20adapters%20with
#                       https://www.usb.org/sites/default/files/D2T2-1%20-%20USB%20Power%20Delivery.pdf#page=14

IS_PLATFORM_COMPONENT = True

# Configuration parameters
CONF_USB_PD_SINK_ID = "usb_pd_sink_id"
CONF_PDO_PROFILES = "pdo_profiles"
CONF_ON_NEGOTIATION = "on_negotiation"
CONF_ON_CONNECTOR_CONNECTED = "on_source_connected"
CONF_ON_CONNECTOR_DISCONNECTED = "on_source_disconnected"

# Objects
pd_sink_ns = cg.esphome_ns.namespace("usb_pd_sink")
UsbPdSink = pd_sink_ns.class_("UsbPdSink")


# Actions
NegotiateAction = pd_sink_ns.class_("NegotiateAction", automation.Action)


# Automations
OnNegotiationTrigger = pd_sink_ns.class_(
    "OnNegotiationTrigger", automation.Trigger.template()
)
OnSourceConnectedTrigger = pd_sink_ns.class_(
    "OnSourceConnectedTrigger", automation.Trigger.template()
)
OnSourceDisconnectedTrigger = pd_sink_ns.class_(
    "OnSourceDisconnectedTrigger", automation.Trigger.template()
)


# Validations
def validate_voltage(value):
    return cv.voltage(value) * 1000


def validate_ampere(value):
    return cv.int_(cv.current(value) * 1000)


def validate_wattage(value):
    return cv.int_(cv.wattage(value) * 1000)


def validate_duplicates(configs):
    for i, iconfig in enumerate(configs):
        for j, jconfig in enumerate(configs):
            if i == j:
                continue  # skip self check
            if iconfig == jconfig:
                raise cv.Invalid("Identical power rules can not be specified together")
    return configs


# Schemas
USB_PD_SINK_TEMPLATABLE_POWER_RULE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_VOLTAGE): cv.templatable(validate_voltage),
        cv.Optional(CONF_AMPERE): cv.templatable(validate_ampere),
    }
)


PD_SINK_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(UsbPdSink),
        cv.Optional(CONF_PDO_PROFILES): cv.All(
            cv.ensure_list(USB_PD_SINK_TEMPLATABLE_POWER_RULE_SCHEMA),
            cv.Length(min=1, max=3),
            validate_duplicates,
        ),
        cv.Optional(CONF_ON_NEGOTIATION): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnNegotiationTrigger),
            }
        ),
        cv.Optional(CONF_ON_CONNECTOR_CONNECTED): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnSourceConnectedTrigger)}
        ),
        cv.Optional(CONF_ON_CONNECTOR_DISCONNECTED): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnSourceDisconnectedTrigger)}
        ),
    }
)


async def setup_usb_pd_sink_core_(pd_sink_var, config):
    cg.add(pd_sink_var.set_milli_voltage(5000))
    cg.add(pd_sink_var.set_milli_ampere(3000))


async def register_usb_pd_sink(var, config):
    if not CORE.has_id(config[CONF_ID]):
        var = cg.Pvariable(config[CONF_ID], var)
    await setup_usb_pd_sink_core_(var, config)


@automation.register_action(
    "usb_pd_sink.negotiate",
    NegotiateAction,
    USB_PD_SINK_TEMPLATABLE_POWER_RULE_SCHEMA.extend(
        {cv.Required(CONF_ID): cv.use_id(UsbPdSink)}
    ),
)
async def usb_pd_sink_negotiate_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    if CONF_VOLTAGE in config:
        template_ = await cg.templatable(config[CONF_VOLTAGE], args, cg.uint16)
        cg.add(var.set_milli_voltage(template_))
    if CONF_AMPERE in config:
        template_ = await cg.templatable(config[CONF_AMPERE], args, cg.uint16)
        cg.add(var.set_milli_ampere(template_))
    return var


@coroutine_with_priority(100.0)
async def to_code(config):
    cg.add_global(pd_sink_ns.using)
