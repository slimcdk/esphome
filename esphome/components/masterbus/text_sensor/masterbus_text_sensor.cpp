#include "masterbus_text_sensor.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.text_sensor");

void MasterbusTextSensor::publish_masterbus_value(const MasterbusValue &value) {
  switch (value.type) {
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT:
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME:
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE:
      this->publish_state(value.as_text);
      return;
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_LIST_OPTION:
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_DEVICE_ID:
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_EVENTABLE:
      // The device does not check its own index against its list, so it is checked here: an entry
      // beyond the ones configured is shown as unknown rather than as somebody else's name.
      if (value.as_raw >= this->option_count_) {
        ESP_LOGW(TAG, "Field %u reported entry %" PRIu32 ", which is beyond the configured options", this->get_param(),
                 value.as_raw);
        this->publish_masterbus_unavailable();
        return;
      }
      this->publish_state(this->options_[value.as_raw]);
      return;
    default:
      this->publish_masterbus_unavailable();
      return;
  }
}

}  // namespace esphome::masterbus
