#pragma once

#include "esphome/components/masterbus/masterbus.h"
#include "esphome/components/number/number.h"

namespace esphome::masterbus {

class MasterbusNumber : public number::Number, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void publish_masterbus_value(const MasterbusValue &value) override {
    if (value.type != MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT) {
      this->publish_masterbus_unavailable();
      return;
    }
    this->publish_state(value.as_float);
  }
  void publish_masterbus_unavailable() override { this->publish_state(NAN); }

 protected:
  void control(float value) override;
};

}  // namespace esphome::masterbus
