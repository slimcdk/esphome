#include "masterbus_switch.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.switch");

void MasterbusSwitch::write_state(bool state) {
  // Nothing is published here on purpose. The switch follows what the device reports afterwards,
  // so it never claims a state the equipment did not confirm.
  this->write(state ? 1.0f : 0.0f);
}

void MasterbusSwitch::dump_config() {
  LOG_SWITCH("", "MasterBus switch", this);
  this->dump_masterbus_config_();
}

}  // namespace esphome::masterbus
