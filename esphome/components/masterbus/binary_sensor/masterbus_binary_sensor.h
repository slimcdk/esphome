#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/masterbus/masterbus.h"

namespace esphome::masterbus {

class MasterbusBinarySensor : public binary_sensor::BinarySensor, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void dump_config() override;

  void publish_masterbus_boolean(bool value) override { this->publish_state(value); }
  void publish_masterbus_unavailable() override { this->invalidate_state(); }
};

}  // namespace esphome::masterbus
