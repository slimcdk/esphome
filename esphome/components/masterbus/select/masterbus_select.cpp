#include "masterbus_select.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

static const char *const TAG = "masterbus.select";

void MasterbusSelect::publish_masterbus_value(const MasterbusValue &value) {
  if (value.type != MasterbusValueType::MASTERBUS_VALUE_TYPE_LIST_OPTION) {
    this->publish_masterbus_unavailable();
    return;
  }
  if (!this->has_index(value.as_raw)) {
    ESP_LOGW(TAG, "Field %u reported option %" PRIu32 ", which is beyond the configured options", this->get_param(),
             value.as_raw);
    this->publish_masterbus_unavailable();
    return;
  }
  this->publish_state(static_cast<size_t>(value.as_raw));
}

void MasterbusSelect::control(const std::string &value) {
  // The vendor API has no list-option write, so there is no precedent to copy. The field is
  // readable.
  ESP_LOGW(TAG, "Cannot set field %u: MasterBus list fields cannot be written", this->get_param());
}

}  // namespace esphome::masterbus
