#include "masterbus_switch.h"

namespace esphome::masterbus {

void MasterbusSwitch::write_state(bool state) {
  // Nothing is published here on purpose. The switch follows what the device reports afterwards,
  // so it never claims a state the equipment did not confirm.
  this->write(state ? 1.0f : 0.0f);
}

}  // namespace esphome::masterbus
