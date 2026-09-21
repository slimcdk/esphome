#pragma once

#include "esphome/components/masterbus/masterbus.h"
#include "esphome/components/switch/switch.h"

namespace esphome::masterbus {

class MasterbusSwitch : public switch_::Switch, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void publish_masterbus_value(const MasterbusValue &value) override {
    if (value.type != MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN) {
      this->publish_masterbus_unavailable();
      return;
    }
    this->publish_state(value.as_boolean);
  }
  // A switch has no way to clear a published state, so the state flag is all that can be dropped.
  void publish_masterbus_unavailable() override { this->set_has_state(false); }

 protected:
  void write_state(bool state) override;
};

}  // namespace esphome::masterbus
