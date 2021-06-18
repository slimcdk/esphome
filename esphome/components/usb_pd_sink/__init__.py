import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_VOLTAGE, CONF_AMPERAGE
from esphome.core import CORE, coroutine_with_priority


IS_PLATFORM_COMPONENT = True

usb_pd_sink_ns = cg.esphome_ns.namespace("usb_pd_sink")
UsbPdSink = usb_pd_sink_ns.class_("UsbPdSink")

NegotiateAction = usb_pd_sink_ns.class_("NegotiateAction", automation.Action)


USB_PD_VOLTAGES = [5, 9, 12, 15, 20]

USB_PD_SINK_SCHEMA = cv.Schema({cv.Optional(CONF_VOLTAGE, default=5): cv.int_})


async def setup_usb_pd_sink_core_(usb_pd_sink_var, config):
    cg.add(usb_pd_sink_var.set_voltage(config[CONF_VOLTAGE]))


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
            cv.Optional(CONF_VOLTAGE): cv.templatable(cv.int_),
            cv.Optional(CONF_AMPERAGE): cv.templatable(cv.float_),
        }
    ),
)
async def usb_pd_sink_negotiate_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    # yield cg.register_parented(var, config[CONF_ID]) # https://github.com/glmnet/esphome/blob/f26f53a82343986b7c9cc064efc54d629f622b05/esphome/components/tmc2209/stepper.py#L56

    if CONF_VOLTAGE in config:
        template_ = await cg.templatable(config[CONF_VOLTAGE], args, cg.int32)
        cg.add(var.set_voltage(template_))

    if CONF_AMPERAGE in config:
        template_ = await cg.templatable(config[CONF_AMPERAGE], args, cg.int32)
        cg.add(var.set_amperage(template_))

    return var


@coroutine_with_priority(100.0)
async def to_code(config):
    cg.add_global(usb_pd_sink_ns.using)
