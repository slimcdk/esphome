import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

# Define the namespace for our hub
tmc_hub_ns = cg.esphome_ns.namespace("tmc_hub")
TMCHub = tmc_hub_ns.class_("TMCHub", cg.Component, uart.UARTDevice)

# The configuration for the Hub itself
CONFIG_SCHEMA = (
    cv.Schema({
        cv.GenerateID(): cv.declare_id(TMCHub),
    })
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    # This connects the Hub to the standard ESPHome UART bus
    await uart.register_uart_device(var, config)

# --- Helper logic for Child Components ---

CONF_TMC_HUB_ID = "tmc_hub_id"

# This schema is used by the Stepper components to reference the Hub
TMC_HUB_DEVICE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TMC_HUB_ID): cv.use_id(TMCHub),
    }
)

async def register_tmc_hub_device(var, config):
    """
    Called by child stepper components to register themselves with the hub.
    """
    parent = await cg.get_variable(config[CONF_TMC_HUB_ID])

    # 1. Sets the 'parent_' member in the C++ child class
    # (assuming the child inherits from Parented<TMCHub>)
    await cg.register_parented(var, parent)

    # 2. Informs the hub about this device for 'dump_config' purposes
    cg.add(parent.add_device_to_hub(str(config[CONF_ID]), config["address"]))
