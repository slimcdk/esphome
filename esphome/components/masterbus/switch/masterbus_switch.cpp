#include "masterbus_switch.h"

namespace esphome::masterbus {

void MasterbusSwitch::write_state(bool state) {
  // Nothing is published here on purpose. The switch follows what the device reports afterwards,
  // so it never claims a state the equipment did not confirm.
  this->get_masterbus_device()->get_hub()->write_boolean(*this, state);
}

}  // namespace esphome::masterbus
