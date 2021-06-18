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

  void set_target(int32_t steps);


  void set_stepper(stepper::Stepper *stepper) { this->_stepper = stepper; };
  void set_encoder(sensor::Sensor *encoder) { this->_encoder = encoder; };
  void set_ratio_hint(float ratio) { this->_ratio_hint = ratio; };

 protected:
  float _ratio_hint;
  stepper::Stepper *_stepper{nullptr};
  sensor::Sensor *_encoder{nullptr};
};

}  // namespace encoder
}  // namespace esphome
