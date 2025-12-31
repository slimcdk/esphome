#include "esphome/components/tmc2224/tmc2224_api_registers.h"
#include "tmc2224_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tmc2224 {

// static const char *const TAG = "tmc2224.sensor";

void ActualCurrentSensor::dump_config() { LOG_SENSOR(" ", "TMC2224 Actual Current Sensor", this); }
void ActualCurrentSensor::update() {
  const uint8_t acs = this->parent_->read_field(CS_ACTUAL_FIELD);
  this->publish_state(this->parent_->current_scale_to_rms_current_mA(acs));
}

void PWMScaleSumSensor::dump_config() { LOG_SENSOR(" ", "TMC2224 PWM Scale Sum Sensor", this); }
void PWMScaleSumSensor::update() { this->publish_state(this->parent_->read_field(PWM_SCALE_SUM_FIELD)); }

void PWMScaleAutoSensor::dump_config() { LOG_SENSOR(" ", "TMC2224 PWM Scale Auto Sensor", this); }
void PWMScaleAutoSensor::update() { this->publish_state(this->parent_->read_field(PWM_SCALE_AUTO_FIELD)); }

void PWMOFSAutoSensor::dump_config() { LOG_SENSOR(" ", "TMC2224 OFS Auto Sensor", this); }
void PWMOFSAutoSensor::update() { this->publish_state(this->parent_->read_field(PWM_OFS_AUTO_FIELD)); }

void PWMGradAutoSensor::dump_config() { LOG_SENSOR(" ", "TMC2224 Grad Auto Sensor", this); }
void PWMGradAutoSensor::update() { this->publish_state(this->parent_->read_field(PWM_GRAD_AUTO_FIELD)); }

}  // namespace tmc2224
}  // namespace esphome
