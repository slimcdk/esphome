#pragma once

#include "esphome/components/masterbus/masterbus.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::masterbus {

class MasterbusSensor : public sensor::Sensor, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void publish_masterbus_value(const MasterbusValue &value) override;
  void publish_masterbus_unavailable() override { this->publish_state(NAN); }
};

}  // namespace esphome::masterbus
