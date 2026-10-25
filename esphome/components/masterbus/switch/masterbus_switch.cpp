#include "masterbus_switch.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.switch");

void MasterbusSwitch::write_state(bool state) { this->write(state ? 1.0f : 0.0f); }

void MasterbusSwitch::dump_config() {
  LOG_SWITCH("", "MasterBus switch", this);
  this->dump_masterbus_config_();
}

}  // namespace esphome::masterbus
