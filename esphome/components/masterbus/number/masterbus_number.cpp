#include "masterbus_number.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.number");

void MasterbusNumber::control(float value) {
  // The same frame that fires a boolean field carries an arbitrary float, so a numeric write needs
  // nothing of its own.
  this->write(value);
}

void MasterbusNumber::dump_config() {
  LOG_NUMBER("", "MasterBus number", this);
  this->dump_masterbus_config_();
}

}  // namespace esphome::masterbus
