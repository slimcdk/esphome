#include "masterbus_button.h"

namespace esphome::masterbus {

void MasterbusButton::press_action() { this->get_masterbus_device()->get_hub()->write_boolean(*this, true); }

}  // namespace esphome::masterbus
