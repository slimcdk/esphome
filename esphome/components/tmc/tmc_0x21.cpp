#include "tmc_0x21.h"
#include "tmc_config_dumps.h"

namespace esphome {
namespace tmc {

void TMC0X21::dump_config() {
  TMCStepper::dump_config();
  LOG_0X21TMC(this);
}

void TMC0X21::setup() {
  ESP_LOGCONFIG(this->model_, "Setting up TMC0X21 Stepper...");

  this->high_freq_.start();

  if (this->enn_pin_ != nullptr) {
    this->enn_pin_->setup();
  }

  if (this->index_pin_ != nullptr) {
    this->index_pin_->setup();
  }

  if (this->step_pin_ != nullptr and this->dir_pin_ != nullptr) {
    this->step_pin_->setup();
    this->dir_pin_->setup();
  }

  if (this->diag_pin_ != nullptr) {
    this->diag_pin_->setup();
    this->diag_pin_->attach_interrupt(ISRPinTriggerStore::pin_isr, &this->diag_isr_store_, gpio::INTERRUPT_RISING_EDGE);
    this->diag_isr_store_.pin_triggered_ptr = &this->diag_triggered_;
  }

  if (!this->read_field(VERSION_FIELD)) {
    this->status_set_error(LOG_STR("Failed to communicate with driver"));
    this->mark_failed();
  }

  this->write_field(PDN_DISABLE_FIELD, true);
  this->write_field(TEST_MODE_FIELD, false);
  this->write_field(SHAFT_FIELD, false);
  this->write_field(MSTEP_REG_SELECT_FIELD, true);
  this->write_field(INTERNAL_RSENSE_FIELD, !this->rsense_.has_value());
  this->write_field(I_SCALE_ANALOG_FIELD, this->use_analog_current_scale_);

  if (this->vsense_.has_value()) {
    this->write_field(VSENSE_FIELD, this->vsense_.value());
  }

  if (this->ottrim_.has_value()) {
    this->write_field(OTTRIM_FIELD, this->ottrim_.value());
  }

  if (this->toff_recovery_ && !this->toff_storage_.has_value()) {
    const uint8_t toff_ = this->read_field(TOFF_FIELD);
    if (toff_ == 0) {
      ESP_LOGW(this->model_,
               "captured TOFF value was 0 and will not be used for recovery (as it will disable the driver)");
    } else {
      this->toff_storage_ = toff_;
    }
  }

  this->diag_handler_.set_callbacks(  // DIAG
      [this]() {
        ESP_LOGV(this->model_, "Executing DIAG rise event");
        // TODO: Handle Power-on reset ??
        const int32_t gstat = this->read_register(GSTAT);
        this->check_gstat_ = (bool) gstat;

        // this->stall_handler_.check(gstat == 0b000);
        if (gstat == 0b000) {
          this->on_stall_callback_.call();
        }

        this->reset_handler_.check((bool) this->extract_field(gstat, RESET_FIELD));
        this->drv_err_handler_.check((bool) this->extract_field(gstat, DRV_ERR_FIELD));
        this->uvcp_handler_.check((bool) this->extract_field(gstat, UV_CP_FIELD));

        this->on_driver_status_callback_.call(DIAG_TRIGGERED);
      },  // rise
      [this]() {
        ESP_LOGV(this->model_, "Executing DIAG fall event");
        const int32_t gstat = this->read_register(GSTAT);
        this->check_gstat_ = (bool) gstat;
        this->reset_handler_.check((bool) this->extract_field(gstat, RESET_FIELD));
        this->drv_err_handler_.check((bool) this->extract_field(gstat, DRV_ERR_FIELD));
        this->uvcp_handler_.check((bool) this->extract_field(gstat, UV_CP_FIELD));

        this->on_driver_status_callback_.call(DIAG_TRIGGER_CLEARED);
      }  // fall
  );

  this->stall_handler_.set_on_rise_callback([this]() { this->on_stall_callback_.call(); });

  this->reset_handler_.set_callbacks(  // gstat reset
      [this]() {                       // rise
        this->write_field(RESET_FIELD, 1);
        this->on_driver_status_callback_.call(RESET);
      },
      [this]() {  // fall
        this->write_field(RESET_FIELD, 1);
        this->on_driver_status_callback_.call(RESET_CLEARED);
      });

  this->drv_err_handler_.set_callbacks(  // gstat drv_err
      [this]() {                         // rise
        this->write_field(DRV_ERR_FIELD, 1);

        const int32_t drv_status = this->read_register(DRV_STATUS);
        this->ot_handler_.check((bool) this->extract_field(drv_status, OT_FIELD));
        this->otpw_handler_.check((bool) this->extract_field(drv_status, OTPW_FIELD));
        this->t157_handler_.check((bool) this->extract_field(drv_status, T157_FIELD));
        this->t150_handler_.check((bool) this->extract_field(drv_status, T150_FIELD));
        this->t143_handler_.check((bool) this->extract_field(drv_status, T143_FIELD));
        this->t120_handler_.check((bool) this->extract_field(drv_status, T120_FIELD));
        this->ola_handler_.check((bool) this->extract_field(drv_status, OLA_FIELD));
        this->olb_handler_.check((bool) this->extract_field(drv_status, OLB_FIELD));
        this->s2vsa_handler_.check((bool) this->extract_field(drv_status, S2VSA_FIELD));
        this->s2vsb_handler_.check((bool) this->extract_field(drv_status, S2VSB_FIELD));
        this->s2ga_handler_.check((bool) this->extract_field(drv_status, S2GA_FIELD));
        this->s2gb_handler_.check((bool) this->extract_field(drv_status, S2GB_FIELD));
        this->on_driver_status_callback_.call(DRIVER_ERROR);
      },
      [this]() {  // fall
        ESP_LOGV(this->model_, "Executing driver err fall event");
        this->on_driver_status_callback_.call(DRIVER_ERROR_CLEARED);
      });

  this->uvcp_handler_.set_callbacks(  // gstat uc_vp
      [this]() {                      // rise
        this->write_field(UV_CP_FIELD, 1);
        this->on_driver_status_callback_.call(CP_UNDERVOLTAGE);
      },
      [this]() {  // fall
        ESP_LOGV(this->model_, "Executing GSTAT UCVP fall event");
        this->on_driver_status_callback_.call(CP_UNDERVOLTAGE_CLEARED);
      });

  this->otpw_handler_.set_callbacks(  // drv_status overtemperature prewarning
      [this]() { this->on_driver_status_callback_.call(OVERTEMPERATURE_PREWARNING); },
      [this]() { this->on_driver_status_callback_.call(OVERTEMPERATURE_PREWARNING_CLEARED); });

  this->ot_handler_.set_callbacks(  // drv_status overtemperature
      [this]() { this->on_driver_status_callback_.call(OVERTEMPERATURE); },
      [this]() { this->on_driver_status_callback_.call(OVERTEMPERATURE_CLEARED); });

  this->t120_handler_.set_callbacks(                                                // drv_status t120 flag
      [this]() { this->on_driver_status_callback_.call(TEMPERATURE_ABOVE_120C); },  // rise
      [this]() { this->on_driver_status_callback_.call(TEMPERATURE_BELOW_120C); }   // fall
  );

  this->t143_handler_.set_callbacks(                                                // drv_status t143 flag
      [this]() { this->on_driver_status_callback_.call(TEMPERATURE_ABOVE_143C); },  // rise
      [this]() { this->on_driver_status_callback_.call(TEMPERATURE_BELOW_143C); }   // fall
  );

  this->t150_handler_.set_callbacks(                                                // drv_status t150 flag
      [this]() { this->on_driver_status_callback_.call(TEMPERATURE_ABOVE_150C); },  // rise
      [this]() { this->on_driver_status_callback_.call(TEMPERATURE_BELOW_150C); }   // fall
  );

  this->t157_handler_.set_callbacks(                                                // drv_status t157 flag
      [this]() { this->on_driver_status_callback_.call(TEMPERATURE_ABOVE_157C); },  // rise
      [this]() { this->on_driver_status_callback_.call(TEMPERATURE_BELOW_157C); }   // fall
  );

  this->ola_handler_.set_callbacks(  // drv_status ola
      [this]() {
        this->on_driver_status_callback_.call(OPEN_LOAD);
        this->on_driver_status_callback_.call(OPEN_LOAD_A);
      },
      [this]() {
        this->on_driver_status_callback_.call(OPEN_LOAD_CLEARED);
        this->on_driver_status_callback_.call(OPEN_LOAD_A_CLEARED);
      });

  this->olb_handler_.set_callbacks(  // drv_status olb
      [this]() {
        this->on_driver_status_callback_.call(OPEN_LOAD);
        this->on_driver_status_callback_.call(OPEN_LOAD_B);
      },
      [this]() {
        this->on_driver_status_callback_.call(OPEN_LOAD_CLEARED);
        this->on_driver_status_callback_.call(OPEN_LOAD_B_CLEARED);
      });

  this->s2vsa_handler_.set_callbacks(  // drv_status s2vsa
      [this]() {
        this->on_driver_status_callback_.call(LOW_SIDE_SHORT);
        this->on_driver_status_callback_.call(LOW_SIDE_SHORT_A);
      },
      [this]() {
        this->on_driver_status_callback_.call(LOW_SIDE_SHORT_CLEARED);
        this->on_driver_status_callback_.call(LOW_SIDE_SHORT_A_CLEARED);
      });

  this->s2vsb_handler_.set_callbacks(  // drv_status s2vsb
      [this]() {
        this->on_driver_status_callback_.call(LOW_SIDE_SHORT);
        this->on_driver_status_callback_.call(LOW_SIDE_SHORT_B);
      },
      [this]() {
        this->on_driver_status_callback_.call(LOW_SIDE_SHORT_CLEARED);
        this->on_driver_status_callback_.call(LOW_SIDE_SHORT_B_CLEARED);
      });

  this->s2ga_handler_.set_callbacks(  // drv_status s2ga
      [this]() {
        this->on_driver_status_callback_.call(GROUND_SHORT);
        this->on_driver_status_callback_.call(GROUND_SHORT_A);
      },
      [this]() {
        this->on_driver_status_callback_.call(GROUND_SHORT_CLEARED);
        this->on_driver_status_callback_.call(GROUND_SHORT_A_CLEARED);
      });

  this->s2gb_handler_.set_callbacks(  // drv_status s2gb
      [this]() {
        this->on_driver_status_callback_.call(GROUND_SHORT);
        this->on_driver_status_callback_.call(GROUND_SHORT_B);
      },
      [this]() {
        this->on_driver_status_callback_.call(GROUND_SHORT_CLEARED);
        this->on_driver_status_callback_.call(GROUND_SHORT_B_CLEARED);
      });

  this->high_freq_.start();

  this->write_field(VACTUAL_FIELD, 0);

  if (this->control_method_ == ControlMethod::PULSES_CONTROL) {
    this->write_field(MULTISTEP_FILT_FIELD, false);
    this->write_field(DEDGE_FIELD, true);
  }

  if (this->control_method_ == ControlMethod::SERIAL_CONTROL) {
    /* Configure INDEX for pulse feedback from the driver */
    // Check mux from figure 15.1 from datasheet rev1.09
    this->write_field(DEDGE_FIELD, false);
    this->write_field(INDEX_OTPW_FIELD, false);
    this->write_field(INDEX_STEP_FIELD, true);
    this->ips_.current_position_ptr = &this->current_position;
    this->ips_.direction_ptr = &this->current_direction;
    this->index_pin_->attach_interrupt(IndexPulseStore::pulse_isr, &this->ips_, gpio::INTERRUPT_ANY_EDGE);
  }

  this->enable(true);

  ESP_LOGCONFIG(this->model_, "TMC0X21 Stepper setup done.");
}

void TMC0X21::loop() {
  if (this->driver_health_check_is_enabled_ or this->stall_detection_is_enabled_) {
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

bool TMC0X21::is_stalled() {
  if (this->current_direction == Direction::STANDSTILL) {
    return false;
  }

  const int32_t sgthrs = this->read_register(SGTHRS);
  const int32_t sgresult = this->read_register(SG_RESULT);
  return (sgthrs << 1) > sgresult;
}

}  // namespace tmc
}  // namespace esphome
