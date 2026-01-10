#include "tmc_0x20.h"
#include "tmc_config_dumps.h"

namespace esphome {
namespace tmc {

void TMC0X20::dump_config() {
  TMCStepper::dump_config();
  LOG_0X20TMC(this);
}

void TMC0X20::loop() {
  if (this->driver_health_check_is_enabled_) {
    if (this->diag_pin_ != nullptr) {
      this->diag_handler_.check(this->diag_triggered_);
      if (this->diag_triggered_) {
        this->diag_triggered_ = this->diag_pin_->digital_read();  // don't clear flag if DIAG is still up
      }
    } else {
      const int32_t ioin = this->read_register(IOIN);
      this->diag_handler_.check((bool) this->extract_field(ioin, DIAG_FIELD));
      // TODO: maybe do something with INDEX for warnings
    }
  }

  // Compute speed and direction
  const time_t now = micros();
  this->calculate_speed_(now);
  const int32_t to_target = (this->target_position - this->current_position);
  this->current_direction = (to_target != 0 ? (Direction) (to_target / abs(to_target)) : Direction::STANDSTILL);

  int32_t vactual_ = this->speed_to_vactual(this->current_speed_);

  if (this->control_method_ == ControlMethod::SERIAL_CONTROL) {
    vactual_ *= this->current_direction;
    if (this->vactual_ != vactual_) {
      this->write_field(VACTUAL_FIELD, vactual_);
      this->vactual_ = vactual_;
    }
  }

  if (this->control_method_ == ControlMethod::PULSES_CONTROL) {
    time_t dt = now - this->last_step_;
    if (dt >= (1 / (float) vactual_) * 1e6f) {
      if (this->direction_ != this->current_direction) {
        this->dir_pin_->digital_write(this->current_direction == Direction::BACKWARD);
        this->direction_ = this->current_direction;
      }
      this->step_pin_->digital_write(this->step_state_);
      this->step_state_ = !this->step_state_;
      this->current_position += (int32_t) this->current_direction;
      this->last_step_ = now;
    }
  }
}

}  // namespace tmc
}  // namespace esphome
