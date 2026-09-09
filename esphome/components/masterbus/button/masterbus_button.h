#pragma once

#include "esphome/components/button/button.h"
#include "esphome/components/masterbus/masterbus.h"

namespace esphome::masterbus {

class MasterbusButton : public button::Button, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  // A push button field carries no state to report or to lose.
  void publish_masterbus_value(const MasterbusValue &value) override {}
  void publish_masterbus_unavailable() override {}

 protected:
  void press_action() override;
};

}  // namespace esphome::masterbus
