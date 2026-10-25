#pragma once

#include "esphome/components/masterbus/masterbus.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::masterbus {

class MasterbusSensor final : public sensor::Sensor, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void dump_config() override;

  void publish_masterbus_float(float value) override { this->publish_state(value); }
  void publish_masterbus_boolean(bool value) override { this->publish_state(value ? 1.0f : 0.0f); }
  void publish_masterbus_index(uint32_t index) override { this->publish_state(static_cast<float>(index)); }
  void publish_masterbus_unavailable() override { this->publish_state(NAN); }
};

}  // namespace esphome::masterbus
