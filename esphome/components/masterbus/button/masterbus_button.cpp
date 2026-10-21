#include "masterbus_button.h"

namespace esphome::masterbus {

// An event such as "Close relay" reads false whatever it did, so there is nothing to read back.
void MasterbusButton::press_action() { this->write(1.0f, false); }

}  // namespace esphome::masterbus
