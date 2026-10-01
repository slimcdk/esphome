import esphome.codegen as cg
from esphome.types import ConfigType
from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # The inverter-online binary sensor is optional, so nothing in the component pulls
    # binary_sensor in, and the tests need it to see what reaches the entity. The log callback is
    # how a test reads what the component says.
    async def to_code_testing(config: ConfigType) -> None:
        cg.add_define("USE_BINARY_SENSOR")
        cg.add_define("ESPHOME_ENTITY_BINARY_SENSOR_COUNT", 1)
        cg.add_define("USE_LOG_LISTENERS")
        cg.add_define("ESPHOME_LOG_MAX_LISTENERS", 2)

    manifest.to_code = to_code_testing
    manifest.dependencies = manifest.dependencies + ["binary_sensor"]
    # to_code only runs for a component the harness put in the config, and it initialises every
    # MULTI_CONF component as an empty list. Dropping that for the test build is what lets the
    # defines above be emitted.
    manifest.multi_conf = False
