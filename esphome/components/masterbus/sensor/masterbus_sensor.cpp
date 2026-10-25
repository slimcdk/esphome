#include "masterbus_sensor.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.sensor");

void MasterbusSensor::dump_config() {
  LOG_SENSOR("", "MasterBus sensor", this);
  this->dump_masterbus_config_();
}

}  // namespace esphome::masterbus
