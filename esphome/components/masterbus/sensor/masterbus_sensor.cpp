#include "masterbus_sensor.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.sensor");

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

void MasterbusSensor::dump_config() {
  LOG_SENSOR("", "MasterBus sensor", this);
  this->dump_masterbus_config_();
}

}  // namespace esphome::masterbus
