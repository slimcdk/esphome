#include "masterbus_select.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.select");

void MasterbusSelect::publish_masterbus_index(uint32_t index) {
  if (!this->has_index(index)) {
    ESP_LOGW(TAG, "Field %u reported option %" PRIu32 ", which is beyond the configured options", this->get_param(),
             index);
    this->publish_masterbus_unavailable();
    return;
  }
  this->publish_state(static_cast<size_t>(index));
}

void MasterbusSelect::control(size_t index) {
  // A list option travels as its index, in the same float a number would use.
  this->write(static_cast<float>(index));
}

void MasterbusSelect::dump_config() {
  LOG_SELECT("", "MasterBus select", this);
  this->dump_masterbus_config_();
}

}  // namespace esphome::masterbus
