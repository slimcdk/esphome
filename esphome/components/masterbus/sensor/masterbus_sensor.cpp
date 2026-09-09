#include "masterbus_sensor.h"

namespace esphome::masterbus {

void MasterbusSensor::publish_masterbus_value(const MasterbusValue &value) {
  switch (value.type) {
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT:
      this->publish_state(value.as_float);
      break;
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN:
      this->publish_state(value.as_boolean ? 1.0f : 0.0f);
      break;
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_LIST_OPTION:
      this->publish_state(static_cast<float>(value.as_raw));
      break;
    default:
      this->publish_state(NAN);
      break;
  }
}

}  // namespace esphome::masterbus
