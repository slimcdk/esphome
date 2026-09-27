#include "bms_emulator.h"

#include <cmath>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome::bms_emulator {

static const char *const TAG = "bms_emulator";

namespace gp = growatt_pylontech;

void BmsEmulator::setup() {
  this->tx_.reserve(canbus::CAN_MAX_DATA_LENGTH);
  this->canbus_->add_callback([this](uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &) {
    this->on_frame(can_id, extended_id, rtr, App.get_loop_component_start_time());
  });
#ifdef USE_BINARY_SENSOR
  if (this->inverter_online_sensor_ != nullptr)
    this->inverter_online_sensor_->publish_initial_state(false);
#endif
}

void BmsEmulator::dump_config() {
  ESP_LOGCONFIG(
      TAG,
      "BMS emulator:\n"
      "  Type: %s\n"
      "  Inverter frame: 0x%03" PRIX32 ", timeout %" PRIu32 " ms\n"
      "  Modules: %u",
      LOG_STR_ARG(this->type_ == BmsEmulatorType::BMS_EMULATOR_TYPE_GROWATT_PYLONTECH ? LOG_STR("growatt_pylontech")
                                                                                      : LOG_STR("unknown")),
      this->inverter_frame_id_, this->inverter_timeout_ms_, this->module_count_);
  LOG_UPDATE_INTERVAL(this);
}

void BmsEmulator::update() {
  this->check_inverter(App.get_loop_component_start_time());

  if (!this->has_battery_data_()) {
    if (this->feeding_) {
      ESP_LOGW(TAG, "A battery measurement has no value; the inverter is not being fed");
    }
    this->feeding_ = false;
    return;
  }
  if (!this->feeding_) {
    ESP_LOGI(TAG, "Battery data present; feeding the inverter");
  }
  this->feeding_ = true;

  switch (this->type_) {
    case BmsEmulatorType::BMS_EMULATOR_TYPE_GROWATT_PYLONTECH:
      this->send_growatt_pylontech_();
      break;
  }
}

bool BmsEmulator::has_battery_data_() const {
  for (const sensor::Sensor *measurement :
       {this->voltage_, this->current_, this->state_of_charge_, this->temperature_}) {
    if (measurement == nullptr || std::isnan(measurement->state))
      return false;
  }
  return true;
}

void BmsEmulator::send_growatt_pylontech_() {
  // In the order the inverter has always been sent them.
  this->alive_counter_++;
  this->send_(gp::ALIVE_ID, gp::alive(this->alive_counter_));
  this->send_(gp::MANUFACTURER_ID, gp::manufacturer());
  this->send_(gp::REQUEST_ID, gp::request(this->charge_enabled_.value(), this->discharge_enabled_.value()));
  this->send_(gp::MEASUREMENTS_ID,
              gp::measurements(this->voltage_->state, this->current_->state, this->temperature_->state));
  this->send_(gp::STATE_ID, gp::state(this->state_of_charge_->state, this->state_of_health_.value()));
  this->send_(gp::LIMITS_ID, gp::limits(this->charge_voltage_.value(), this->charge_current_limit_.value(),
                                        this->discharge_current_limit_.value(), this->discharge_voltage_.value()));
  this->send_(gp::ALARMS_ID, gp::alarms(this->module_count_));
}

void BmsEmulator::send_(uint32_t can_id, const gp::Frame &frame) {
  this->tx_.assign(frame.data.begin(), frame.data.begin() + frame.length);
  const canbus::Error error = this->canbus_->send_data(can_id, false, false, this->tx_);
  if (error != canbus::ERROR_OK) {
    ESP_LOGV(TAG, "Frame 0x%03" PRIX32 " not sent: error %d", can_id, static_cast<int>(error));
  }
}

void BmsEmulator::on_frame(uint32_t can_id, bool extended_id, bool rtr, uint32_t now) {
  // A remote request carries nothing from the inverter, and an extended identifier is another frame.
  if (extended_id || rtr || can_id != this->inverter_frame_id_)
    return;
  this->inverter_heard_at_ = now;
  this->set_inverter_online_(true);
}

void BmsEmulator::check_inverter(uint32_t now) {
  if (this->inverter_online_ && now - this->inverter_heard_at_ >= this->inverter_timeout_ms_)
    this->set_inverter_online_(false);
}

void BmsEmulator::set_inverter_online_(bool online) {
  if (online == this->inverter_online_)
    return;
  this->inverter_online_ = online;
  if (online) {
    ESP_LOGI(TAG, "Inverter is on the bus");
  } else {
    ESP_LOGW(TAG, "Inverter has not been heard for %" PRIu32 " ms", this->inverter_timeout_ms_);
  }
#ifdef USE_BINARY_SENSOR
  if (this->inverter_online_sensor_ != nullptr)
    this->inverter_online_sensor_->publish_state(online);
#endif
}

}  // namespace esphome::bms_emulator
