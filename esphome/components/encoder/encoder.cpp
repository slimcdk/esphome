#include "encoder.h"
#include "esphome/core/log.h"

namespace esphome {
namespace encoder {

static const char *TAG = "encoder.stepper";

void StepperEncoder::dump_config() {
  ESP_LOGCONFIG(TAG, "TMC2209:");
  ESP_LOGCONFIG(TAG, "  Ratio: %d", this->_ratio_hint);
  LOG_SENSOR("  ", "Encoder sensor", this->_encoder);
  // LOG_STEPPER("  ", "Stepper", this->_stepper);
  LOG_STEPPER(this);
};

void StepperEncoder::setup() {
  ESP_LOGCONFIG(TAG, "Setting up stepper encoder...");
  this->_encoder->publish_state(0);
};

void StepperEncoder::loop() {
  this->current_position = (int32_t)(this->_encoder->get_state());
  
  bool at_target = this->has_reached_target();

};

void StepperEncoder::set_target(int32_t steps) { 
  this->target_position = steps;
  this->_stepper->set_target(steps);
  // int32_t target_delta = this->target_position * this->_ratio_hint;
  // this->_stepper->target_position = target_delta;
};


} // namespace encoder
} // namespace esphome