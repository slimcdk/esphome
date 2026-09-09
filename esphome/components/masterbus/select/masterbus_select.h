#pragma once

#include "esphome/components/masterbus/masterbus.h"
#include "esphome/components/select/select.h"

namespace esphome::masterbus {

class MasterbusSelect : public select::Select, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void publish_masterbus_value(const MasterbusValue &value) override;
  // A select has no way to clear a published option, so the state flag is all that can be dropped.
  void publish_masterbus_unavailable() override { this->set_has_state(false); }

 protected:
  void control(const std::string &value) override;
};

}  // namespace esphome::masterbus
