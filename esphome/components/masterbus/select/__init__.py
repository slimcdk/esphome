from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_OPTIONS
from esphome.types import ConfigType

from .. import (
    LIST_VALUE_TYPES,
    MasterbusEntity,
    entity_schema,
    masterbus_ns,
    new_entity,
    option_names,
    raw_options,
    set_device_options,
    validate_options,
)

CODEOWNERS = ["@slimcdk"]
DEPENDENCIES = ["masterbus"]

MasterbusSelect = masterbus_ns.class_("MasterbusSelect", select.Select, MasterbusEntity)


def _unique_options(config: ConfigType) -> ConfigType:
    """A select writes the index of the option chosen, so a second copy would write the first's."""
    seen: set[str] = set()
    for option in option_names(config):
        if option in seen:
            raise cv.Invalid(
                f"'{option}' is listed twice. The select writes the index of the option chosen, "
                f"so both would write the index of the first; give the second its own name"
            )
        seen.add(option)
    return config


# The option names come from the device's own string table, which a scan reports. They are listed
# here in the order the device numbers them, so the reported index picks the right one. A device
# list's options name the device each stands for instead.
CONFIG_SCHEMA = cv.All(
    select.select_schema(MasterbusSelect)
    .extend(
        entity_schema(value_types=LIST_VALUE_TYPES, default_value_type="list_option")
    )
    .extend({cv.Required(CONF_OPTIONS): raw_options}),
    validate_options,
    _unique_options,
)


async def to_code(config: ConfigType) -> None:
    var = await new_entity(select.new_select, config, options=option_names(config))
    await set_device_options(var, config)
