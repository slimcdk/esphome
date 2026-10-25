#pragma once

#include "esphome/components/masterbus/masterbus.h"
#include "esphome/components/switch/switch.h"

namespace esphome::masterbus {

class MasterbusSwitch final : public switch_::Switch, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void dump_config() override;

  void publish_masterbus_boolean(bool value) override { this->publish_state(value); }
  // A switch has no way to clear a published state, so the state flag is all that can be dropped.
  void publish_masterbus_unavailable() override { this->set_has_state(false); }

 protected:
  void write_state(bool state) override;
};

}  // namespace esphome::masterbus
