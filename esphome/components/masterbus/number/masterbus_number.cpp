#include "masterbus_number.h"

namespace esphome::masterbus {

void MasterbusNumber::control(float value) {
  // The same frame that fires a boolean field carries an arbitrary float, so a numeric write needs
  // nothing of its own.
  // Nothing is published here on purpose: the number follows what the device reports afterwards,
  // so it never claims a value the equipment did not confirm.
  this->write(value);
}

}  // namespace esphome::masterbus
