#pragma once

#include <vector>

#include "esphome/components/canbus/canbus.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#include "growatt_pylontech.h"

namespace esphome::bms_emulator {

enum class BmsEmulatorType : uint8_t {
  BMS_EMULATOR_TYPE_GROWATT_PYLONTECH = 0,
};

/** Plays a battery's BMS to an inverter on a CAN bus.
 *
 * The battery's measurements come from sensors and its limits and flags from templatable values;
 * every update the frame set of the configured type goes out. A measurement without a value sends
 * nothing at all, so that the inverter's own BMS timeout takes over instead of a stale reading.
 */
class BmsEmulator : public PollingComponent {
 public:
  BmsEmulator(canbus::Canbus *canbus, BmsEmulatorType type) : canbus_(canbus), type_(type) {}

  void setup() override;
  void update() override;
  void dump_config() override;

  void set_voltage_sensor(sensor::Sensor *sensor) { this->voltage_ = sensor; }
  void set_current_sensor(sensor::Sensor *sensor) { this->current_ = sensor; }
  void set_state_of_charge_sensor(sensor::Sensor *sensor) { this->state_of_charge_ = sensor; }
  void set_temperature_sensor(sensor::Sensor *sensor) { this->temperature_ = sensor; }

  template<typename V> void set_state_of_health(V value) { this->state_of_health_ = value; }
  template<typename V> void set_charge_voltage(V value) { this->charge_voltage_ = value; }
  template<typename V> void set_discharge_voltage(V value) { this->discharge_voltage_ = value; }
  template<typename V> void set_charge_current_limit(V value) { this->charge_current_limit_ = value; }
  template<typename V> void set_discharge_current_limit(V value) { this->discharge_current_limit_ = value; }
  template<typename V> void set_charge_enabled(V value) { this->charge_enabled_ = value; }
  template<typename V> void set_discharge_enabled(V value) { this->discharge_enabled_ = value; }
  void set_module_count(uint8_t count) { this->module_count_ = count; }

  void set_inverter_frame_id(uint32_t can_id) { this->inverter_frame_id_ = can_id; }
  void set_inverter_timeout(uint32_t timeout_ms) { this->inverter_timeout_ms_ = timeout_ms; }
#ifdef USE_BINARY_SENSOR
  void set_inverter_online_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->inverter_online_sensor_ = sensor;
  }
#endif

  /// Take a frame from the bus, heard at the given time. Registered as the CAN receive callback in
  /// setup(); public so that a test can drive the clock.
  void on_frame(uint32_t can_id, bool extended_id, bool rtr, uint32_t now);
  /// Count the inverter as gone once its frame has been missing for the timeout. Called every
  /// update; public so that a test can drive the clock.
  void check_inverter(uint32_t now);
  bool is_inverter_online() const { return this->inverter_online_; }

 protected:
  bool has_battery_data_() const;
  void send_growatt_pylontech_();
  void send_(uint32_t can_id, const growatt_pylontech::Frame &frame);
  void set_inverter_online_(bool online);

  canbus::Canbus *canbus_;
  BmsEmulatorType type_;

  sensor::Sensor *voltage_{nullptr};
  sensor::Sensor *current_{nullptr};
  sensor::Sensor *state_of_charge_{nullptr};
  sensor::Sensor *temperature_{nullptr};
  TemplatableValue<float> state_of_health_{};
  TemplatableValue<float> charge_voltage_{};
  TemplatableValue<float> discharge_voltage_{};
  TemplatableValue<float> charge_current_limit_{};
  TemplatableValue<float> discharge_current_limit_{};
  TemplatableValue<bool> charge_enabled_{};
  TemplatableValue<bool> discharge_enabled_{};
  uint8_t module_count_{1};

  uint32_t inverter_frame_id_{0x301};
  uint32_t inverter_timeout_ms_{5000};
  uint32_t inverter_heard_at_{0};
  bool inverter_online_{false};
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *inverter_online_sensor_{nullptr};
#endif

  uint8_t alive_counter_{0};
  /// Whether the last update sent the frame set, so the change is logged once rather than every cycle.
  bool feeding_{false};
  /// One payload buffer for every frame: the CAN API takes a vector, and a fresh one per frame would
  /// allocate seven times a second for months.
  std::vector<uint8_t> tx_;
};

}  // namespace esphome::bms_emulator
