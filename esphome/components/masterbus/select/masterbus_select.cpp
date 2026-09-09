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
  const auto index = this->index_of(value);
  if (!index.has_value()) {
    ESP_LOGW(TAG, "%s is not one of the configured options for field %u", value.c_str(), this->get_param());
    return;
  }
  // A list option travels as its index, in the same float a number would use.
  // Nothing is published here on purpose: the select follows what the device reports afterwards,
  // so it never claims an option the equipment did not confirm.
  this->get_masterbus_device()->get_hub()->write_value(*this, static_cast<float>(index.value()));
}

}  // namespace esphome::masterbus
