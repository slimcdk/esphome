#pragma once

#include "esphome/core/component.h"
#include "esphome/components/stepper/stepper.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace encoder {


class StepperEncoder : public stepper::Stepper, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_stepper(stepper::Stepper *stepper) { this->_stepper = stepper; };
  void set_encoder(sensor::Sensor *encoder) { this->_encoder = encoder; };

 protected:
  stepper::Stepper *_stepper;
  sensor::Sensor *_encoder;
};

}  // namespace encoder
}  // namespace esphome
