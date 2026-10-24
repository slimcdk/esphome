#include "masterbus_button.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.button");

// An event such as "Close relay" reads false whatever it did, so there is nothing to read back.
void MasterbusButton::press_action() { this->write(1.0f, false); }

void MasterbusButton::dump_config() {
  LOG_BUTTON("", "MasterBus button", this);
  this->dump_masterbus_config_();
}

}  // namespace esphome::masterbus
