#pragma once

#include "esphome/components/button/button.h"
#include "esphome/components/masterbus/masterbus.h"

namespace esphome::masterbus {

class MasterbusButton final : public button::Button, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void dump_config() override;

 protected:
  void press_action() override;
};

}  // namespace esphome::masterbus
