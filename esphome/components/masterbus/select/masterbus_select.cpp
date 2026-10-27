#include "masterbus_select.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.select");

void MasterbusSelect::publish_masterbus_index(uint32_t index) {
  if (!this->has_index(index)) {
    ESP_LOGW(TAG, "'%s': field %u of device 0x%06" PRIX32 " reported option %" PRIu32 ", beyond the %u configured",
             LOG_STR_ARG(this->get_log_name()), this->get_param(), this->masterbus_device_->get_address(), index,
             static_cast<unsigned>(this->size()));
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
