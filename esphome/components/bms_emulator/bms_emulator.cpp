#include "bms_emulator.h"

#include <cinttypes>
#include <cmath>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome::bms_emulator {

static const char *const TAG = "bms_emulator";

/// Sent when the configured state of health has no value: the inverter only displays it, and a
/// battery that reports none is better described as healthy than cut off.
static constexpr float STATE_OF_HEALTH_WITHOUT_A_VALUE = 100.0f;

void BmsEmulator::setup() {
  this->tx_.reserve(canbus::CAN_MAX_DATA_LENGTH);
  this->canbus_->add_callback([this](uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &) {
    this->on_frame_(can_id, extended_id, rtr, App.get_loop_component_start_time());
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

  Reading reading;
  if (!this->read_(reading)) {
    if (this->feeding_) {
      ESP_LOGW(TAG, "A battery measurement or limit has no value; the inverter is not being fed");
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
      this->send_growatt_pylontech_(reading);
      break;
  }
}

bool BmsEmulator::read_(Reading &reading) {
  reading.voltage = this->voltage_->state;
  reading.current = this->current_->state;
  reading.state_of_charge = this->state_of_charge_->state;
  reading.temperature = this->temperature_->state;
  reading.charge_voltage = this->charge_voltage_.value();
  reading.discharge_voltage = this->discharge_voltage_.value();
  reading.charge_current_limit = this->charge_current_limit_.value();
  reading.discharge_current_limit = this->discharge_current_limit_.value();
  reading.charge_enabled = this->charge_enabled_.value();
  reading.discharge_enabled = this->discharge_enabled_.value();
  reading.state_of_health = this->state_of_health_.value();
  if (std::isnan(reading.state_of_health))
    reading.state_of_health = STATE_OF_HEALTH_WITHOUT_A_VALUE;

  for (float value :
       {reading.voltage, reading.current, reading.state_of_charge, reading.temperature, reading.charge_voltage,
        reading.discharge_voltage, reading.charge_current_limit, reading.discharge_current_limit}) {
    if (std::isnan(value))
      return false;
  }
  return true;
}

void BmsEmulator::send_growatt_pylontech_(const Reading &reading) {
  namespace protocol = growatt_pylontech;
  this->alive_counter_++;
  if constexpr (protocol::BATTERY_SENDS_ALIVE) {
    if (!this->send_(protocol::ALIVE_ID, protocol::alive(this->alive_counter_)))
      return;
  }
  if (!this->send_(protocol::MANUFACTURER_ID, protocol::manufacturer()) ||
      !this->send_(protocol::REQUEST_ID, protocol::request(reading.charge_enabled, reading.discharge_enabled)) ||
      !this->send_(protocol::MEASUREMENTS_ID,
                   protocol::measurements(reading.voltage, reading.current, reading.temperature)) ||
      !this->send_(protocol::STATE_ID, protocol::state(reading.state_of_charge, reading.state_of_health)) ||
      !this->send_(protocol::LIMITS_ID, protocol::limits(reading.charge_voltage, reading.charge_current_limit,
                                                         reading.discharge_current_limit, reading.discharge_voltage))) {
    return;
  }
  this->send_(protocol::ALARMS_ID, protocol::alarms(this->module_count_));
}

bool BmsEmulator::send_(uint32_t can_id, const growatt_pylontech::Frame &frame) {
  // canbus logs a refused frame itself. Stopping here keeps one cycle from waiting out the
  // controller's queue timeout once for every frame that is left.
  this->tx_.assign(frame.data.begin(), frame.data.begin() + frame.length);
  return this->canbus_->send_data(can_id, false, false, this->tx_) == canbus::ERROR_OK;
}

void BmsEmulator::on_frame_(uint32_t can_id, bool extended_id, bool rtr, uint32_t now) {
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
