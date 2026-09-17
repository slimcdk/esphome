import esphome.codegen as cg
from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # The device and entity lists are StaticVectors sized by slot_counter, so they and their
    # registration methods compile out unless a configuration registers something. The host build
    # has no CAN platform to bind a hub to, so no configuration can register anything here and the
    # counts have to be supplied directly for the decode path to exist at all.
    async def to_code_testing(config):
        cg.add_define("MASTERBUS_DEVICE_COUNT", 4)
        cg.add_define("MASTERBUS_ENTITY_COUNT", 8)
        cg.add_define("USE_MASTERBUS_SCAN")
        cg.add_define("USE_MASTERBUS_TEXT")
        cg.add_define("MASTERBUS_UNKNOWN_FRAME_COUNT", 2)
        cg.add_define("MASTERBUS_SCAN_MAX_DEVICES", 4)

    manifest.to_code = to_code_testing
    # to_code only runs for a component the harness put in the config, and it initialises every
    # MULTI_CONF component as an empty list. Dropping that for the test build is what lets the
    # defines above be emitted.
    manifest.multi_conf = False
