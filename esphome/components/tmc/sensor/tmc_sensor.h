#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/tmc/tmc_api_registers.h"
#include "esphome/components/tmc/tmc_stepper.h"

namespace esphome {
namespace tmc {

class StallGuardResultSensor : public PollingComponent, public sensor::Sensor, public Parented<TMCStepper> {
  void dump_config() override;
  void update() override;
};

class MotorLoadSensor : public PollingComponent, public sensor::Sensor, public Parented<TMCStepper> {
  void dump_config() override;
  void update() override;
};

class ActualCurrentSensor : public PollingComponent, public sensor::Sensor, public Parented<TMCStepper> {
  void dump_config() override;
  void update() override;
};

class PWMScaleSumSensor : public PollingComponent, public sensor::Sensor, public Parented<TMCStepper> {
  void dump_config() override;
  void update() override;
};

class PWMScaleAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMCStepper> {
  void dump_config() override;
  void update() override;
};

class PWMOFSAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMCStepper> {
  void dump_config() override;
  void update() override;
};

class PWMGradAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMCStepper> {
  void dump_config() override;
  void update() override;
};

}  // namespace tmc
}  // namespace esphome
