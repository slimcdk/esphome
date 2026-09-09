#include "masterbus_number.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

static const char *const TAG = "masterbus.number";

void MasterbusNumber::control(float value) {
  // The vendor API has no numeric write, so there is no precedent to copy. The field is readable.
  ESP_LOGW(TAG, "Cannot set field %u: MasterBus numeric fields cannot be written", this->get_param());
}

}  // namespace esphome::masterbus
