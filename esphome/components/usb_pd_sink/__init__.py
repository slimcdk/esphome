import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_VOLTAGE, CONF_AMPERE, CONF_TRIGGER_ID
from esphome.core import CORE, coroutine_with_priority

# Useful links
# General knowledge: https://www.digikey.com/en/articles/designing-in-usb-type-c-and-using-power-delivery-for-rapid-charging
# USB PD Power Rules: https://www.chromium.org/chromium-os/cable-and-adapter-tips-and-tricks#:~:text=DCP%20or%20CDP.-,USB%20PD%20Power%20Rules,-Power%20adapters%20with

IS_PLATFORM_COMPONENT = True

CONF_USB_PD_SINK_ID = "usb_pd_sink_id"
CONF_PDO_PROFILES = "pdo_profiles"
CONF_ON_VOLTAGE_CHANGE = "on_voltage_change"
CONF_ON_AMPERE_CHANGE = "on_ampere_change"
CONF_ON_CONNECTOR_CONNECTED = "on_connector_connected"
CONF_ON_CONNECTOR_DISCONNECTED = "on_connector_disconnected"


pd_sink_ns = cg.esphome_ns.namespace("usb_pd_sink")
UsbPdSink = pd_sink_ns.class_("UsbPdSink")

OnVoltageChangeTrigger = pd_sink_ns.class_("OnVoltageChangeTrigger", automation.Trigger)
OnAmpereChangeTrigger = pd_sink_ns.class_("OnAmpereChangeTrigger", automation.Trigger)
OnConnectorConnectedTrigger = pd_sink_ns.class_(
    "OnConnectorConnectedTrigger", automation.Trigger
)
OnConnectorDisconnectedTrigger = pd_sink_ns.class_(
    "OnConnectorDisconnectedTrigger", automation.Trigger
)

NegotiateAction = pd_sink_ns.class_("NegotiateAction", automation.Action)


def validate_voltage(value):
    mV = cv.voltage(value) * 1000
    return cv.one_of(5000, 9000, 15000, 20000, int=True)(mV)


def validate_ampere(value):
    return cv.int_(cv.current(value) * 1000)


PD_SINK_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ON_VOLTAGE_CHANGE): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnVoltageChangeTrigger),
            }
        ),
        cv.Optional(CONF_ON_AMPERE_CHANGE): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnAmpereChangeTrigger),
            }
        ),
        cv.Optional(CONF_ON_CONNECTOR_CONNECTED): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                    OnConnectorConnectedTrigger
                ),
            }
        ),
        cv.Optional(CONF_ON_CONNECTOR_DISCONNECTED): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                    OnConnectorDisconnectedTrigger
                ),
            }
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
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(UsbPdSink),
            cv.Optional(CONF_VOLTAGE): cv.templatable(validate_voltage),
            cv.Optional(CONF_AMPERE): cv.templatable(validate_ampere),
        }
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
