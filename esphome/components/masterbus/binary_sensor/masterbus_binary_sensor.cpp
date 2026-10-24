#include "masterbus_binary_sensor.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.binary_sensor");

void MasterbusBinarySensor::dump_config() {
  LOG_BINARY_SENSOR("", "MasterBus binary sensor", this);
  this->dump_masterbus_config_();
}

}  // namespace esphome::masterbus
