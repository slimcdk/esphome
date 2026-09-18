#include "masterbus_button.h"

namespace esphome::masterbus {

void MasterbusButton::press_action() { this->device_->get_hub()->write_boolean(*this, true); }

}  // namespace esphome::masterbus
