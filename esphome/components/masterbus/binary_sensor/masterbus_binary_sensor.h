#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/masterbus/masterbus.h"

namespace esphome::masterbus {

class MasterbusBinarySensor : public binary_sensor::BinarySensor, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void publish_masterbus_value(const MasterbusValue &value) override {
    if (value.type != MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN) {
      this->publish_masterbus_unavailable();
      return;
    }
    this->publish_state(value.as_boolean);
  }
  void publish_masterbus_unavailable() override { this->invalidate_state(); }
};

}  // namespace esphome::masterbus
