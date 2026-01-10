#pragma once
#include "esphome/components/tmc2226/tmc2226_api_registers.h"
#include "esphome/components/tmc2226/tmc2226_component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace tmc2226 {

class StallGuardResultSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2226Component> {
  void dump_config() override;
  void update() override;
};

class MotorLoadSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2226Component> {
  void dump_config() override;
  void update() override;
};

class ActualCurrentSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2226Component> {
  void dump_config() override;
  void update() override;
};

class PWMScaleSumSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2226Component> {
  void dump_config() override;
  void update() override;
};

class PWMScaleAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2226Component> {
  void dump_config() override;
  void update() override;
};

class PWMOFSAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2226Component> {
  void dump_config() override;
  void update() override;
};

class PWMGradAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2226Component> {
  void dump_config() override;
  void update() override;
};

}  // namespace tmc2226
}  // namespace esphome
