#include "esphome/components/tmc2202/tmc2202_api_registers.h"
#include "tmc2202_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tmc2202 {

// static const char *const TAG = "tmc2202.sensor";

void ActualCurrentSensor::dump_config() { LOG_SENSOR(" ", "TMC2202 Actual Current Sensor", this); }
void ActualCurrentSensor::update() {
  const uint8_t acs = this->parent_->read_field(CS_ACTUAL_FIELD);
  this->publish_state(this->parent_->current_scale_to_rms_current_mA(acs));
}

void PWMScaleSumSensor::dump_config() { LOG_SENSOR(" ", "TMC2202 PWM Scale Sum Sensor", this); }
void PWMScaleSumSensor::update() { this->publish_state(this->parent_->read_field(PWM_SCALE_SUM_FIELD)); }

void PWMScaleAutoSensor::dump_config() { LOG_SENSOR(" ", "TMC2202 PWM Scale Auto Sensor", this); }
void PWMScaleAutoSensor::update() { this->publish_state(this->parent_->read_field(PWM_SCALE_AUTO_FIELD)); }

void PWMOFSAutoSensor::dump_config() { LOG_SENSOR(" ", "TMC2202 OFS Auto Sensor", this); }
void PWMOFSAutoSensor::update() { this->publish_state(this->parent_->read_field(PWM_OFS_AUTO_FIELD)); }

void PWMGradAutoSensor::dump_config() { LOG_SENSOR(" ", "TMC2202 Grad Auto Sensor", this); }
void PWMGradAutoSensor::update() { this->publish_state(this->parent_->read_field(PWM_GRAD_AUTO_FIELD)); }

}  // namespace tmc2202
}  // namespace esphome
