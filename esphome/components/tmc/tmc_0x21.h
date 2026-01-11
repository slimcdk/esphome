#pragma once

#include "esphome/components/stepper/stepper.h"

#include "tmc_api_registers.h"
#include "tmc_stepper.h"
#include "tmc_events.h"
#include "tmc_config_dumps.h"

namespace esphome {
namespace tmc {

using namespace esphome::stepper;

class TMC0X21 : public TMCStepper {
 public:
  TMC0X21() = default;

  void dump_config() override;
  void setup() override;
  void loop() override;
  bool is_stalled();
};

}  // namespace tmc
}  // namespace esphome
