#pragma once
#include "esphome/components/tmc2224/tmc2224_api_registers.h"
#include "esphome/components/tmc2224/tmc2224_component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace tmc2224 {

class ActualCurrentSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2224Component> {
  void dump_config() override;
  void update() override;
};

class PWMScaleSumSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2224Component> {
  void dump_config() override;
  void update() override;
};

class PWMScaleAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2224Component> {
  void dump_config() override;
  void update() override;
};

class PWMOFSAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2224Component> {
  void dump_config() override;
  void update() override;
};

class PWMGradAutoSensor : public PollingComponent, public sensor::Sensor, public Parented<TMC2224Component> {
  void dump_config() override;
  void update() override;
};

}  // namespace tmc2224
}  // namespace esphome
