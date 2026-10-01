#pragma once

#include <cstdint>
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
 * every update the frame set of the configured type goes out. A measurement or a limit without a
 * value sends nothing at all, so that the inverter's own BMS timeout takes over instead of a stale
 * or guessed reading.
 */
class BmsEmulator final : public PollingComponent {
 public:
  BmsEmulator(canbus::Canbus *canbus, BmsEmulatorType type, sensor::Sensor *voltage, sensor::Sensor *current,
              sensor::Sensor *state_of_charge, sensor::Sensor *temperature)
      : canbus_(canbus),
        type_(type),
        voltage_(voltage),
        current_(current),
        state_of_charge_(state_of_charge),
        temperature_(temperature) {}

  void setup() override;
  void update() override;
  void dump_config() override;

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

  /// Count the inverter as gone once its frame has been missing for the timeout. Called every
  /// update; public so that a test can drive the clock.
  void check_inverter(uint32_t now);
  bool is_inverter_online() const { return this->inverter_online_; }

 protected:
  /// What one cycle sends, read once so that every frame carries the same values.
  struct Reading {
    float voltage, current, state_of_charge, temperature, state_of_health;
    float charge_voltage, discharge_voltage, charge_current_limit, discharge_current_limit;
    bool charge_enabled, discharge_enabled;
  };

  /// A frame from the bus, heard at the given time; the canbus receive callback.
  void on_frame_(uint32_t can_id, bool extended_id, bool rtr, uint32_t now);
  /// The values to send, or false if one the inverter acts on has none.
  bool read_(Reading &reading);
  void send_growatt_pylontech_(const Reading &reading);
  /// False if the controller refused the frame, which ends the cycle.
  bool send_(uint32_t can_id, const growatt_pylontech::Frame &frame);
  void set_inverter_online_(bool online);

  canbus::Canbus *canbus_;
  BmsEmulatorType type_;

  sensor::Sensor *voltage_;
  sensor::Sensor *current_;
  sensor::Sensor *state_of_charge_;
  sensor::Sensor *temperature_;
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
  /// One payload buffer for every frame, reserved in setup(): the CAN API takes a vector, and a new
  /// one per frame would allocate on every send.
  std::vector<uint8_t> tx_;
};

}  // namespace esphome::bms_emulator
