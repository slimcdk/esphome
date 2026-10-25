#pragma once

#include "esphome/components/masterbus/masterbus.h"
#include "esphome/components/number/number.h"

namespace esphome::masterbus {

class MasterbusNumber final : public number::Number, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void dump_config() override;

  void publish_masterbus_float(float value) override { this->publish_state(value); }
  void publish_masterbus_unavailable() override { this->publish_state(NAN); }

 protected:
  void control(float value) override;
};

}  // namespace esphome::masterbus
