#pragma once
#include "esphome/components/tmc2202/tmc2202_api_registers.h"
#include "esphome/components/tmc2202/tmc2202_component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace tmc2202 {

class ActualCurrentSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2202Component> {
  void dump_config() override;
  void update() override;
};

class PWMScaleSumSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2202Component> {
  void dump_config() override;
  void update() override;
};

class PWMScaleAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2202Component> {
  void dump_config() override;
  void update() override;
};

class PWMOFSAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2202Component> {
  void dump_config() override;
  void update() override;
};

class PWMGradAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2202Component> {
  void dump_config() override;
  void update() override;
};

}  // namespace tmc2202
}  // namespace esphome
