#include "masterbus_number.h"

namespace esphome::masterbus {

void MasterbusNumber::control(float value) {
  // The same frame that fires a boolean field carries an arbitrary float, so a numeric write needs
  // nothing of its own. Only the value 1.0 has been seen on the wire, and only on event fields, so
  // this generalises the observed frame rather than reproducing an observed one.
  // Nothing is published here on purpose: the number follows what the device reports afterwards,
  // so it never claims a value the equipment did not confirm.
  this->get_masterbus_device()->get_hub()->write_value(*this, value);
}

}  // namespace esphome::masterbus
