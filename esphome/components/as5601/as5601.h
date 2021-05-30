#pragma once

#include "esphome/components/as560x/as560x.h"


namespace esphome {
namespace as5601 {

class AS5601 : public as560x::AS560X {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
};


}  // namespace as5601
}  // namespace esphome