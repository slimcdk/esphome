#include "esphome/components/tmc2226/tmc2226_api_registers.h"
#include "tmc2226_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tmc2226 {

// static const char *const TAG = "tmc2226.sensor";

void StallGuardResultSensor::dump_config() { LOG_SENSOR(" ", "TMC2226 StallGuard Result Sensor", this); }
void StallGuardResultSensor::update() { this->publish_state(this->parent_->read_register(SG_RESULT)); }

void MotorLoadSensor::dump_config() { LOG_SENSOR(" ", "TMC2226 Motor Load Sensor", this); }
void MotorLoadSensor::update() { this->publish_state(this->parent_->get_motor_load() * 100.0f); }

void ActualCurrentSensor::dump_config() { LOG_SENSOR(" ", "TMC2226 Actual Current Sensor", this); }
void ActualCurrentSensor::update() {
  const uint8_t acs = this->parent_->read_field(CS_ACTUAL_FIELD);
  this->publish_state(this->parent_->current_scale_to_rms_current_mA(acs));
}

void PWMScaleSumSensor::dump_config() { LOG_SENSOR(" ", "TMC2226 PWM Scale Sum Sensor", this); }
void PWMScaleSumSensor::update() { this->publish_state(this->parent_->read_field(PWM_SCALE_SUM_FIELD)); }

void PWMScaleAutoSensor::dump_config() { LOG_SENSOR(" ", "TMC2226 PWM Scale Auto Sensor", this); }
void PWMScaleAutoSensor::update() { this->publish_state(this->parent_->read_field(PWM_SCALE_AUTO_FIELD)); }

void PWMOFSAutoSensor::dump_config() { LOG_SENSOR(" ", "TMC2226 OFS Auto Sensor", this); }
void PWMOFSAutoSensor::update() { this->publish_state(this->parent_->read_field(PWM_OFS_AUTO_FIELD)); }

void PWMGradAutoSensor::dump_config() { LOG_SENSOR(" ", "TMC2226 Grad Auto Sensor", this); }
void PWMGradAutoSensor::update() { this->publish_state(this->parent_->read_field(PWM_GRAD_AUTO_FIELD)); }

}  // namespace tmc2226
}  // namespace esphome
