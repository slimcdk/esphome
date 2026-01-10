#pragma once

#include "esphome/components/stepper/stepper.h"

#include "tmc_api_registers.h"
#include "tmc_stepper.h"
#include "tmc_events.h"
#include "tmc_config_dumps.h"

namespace esphome {
namespace tmc {

class TMC0X20 : public TMCStepper {
 public:
  TMC0X20() = default;

  void dump_config() override;
  void loop() override;
};

}  // namespace tmc
}  // namespace esphome
