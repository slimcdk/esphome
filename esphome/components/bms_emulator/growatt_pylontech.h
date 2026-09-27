#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/// The Pylontech low-voltage CAN frame set, as a Growatt SPH reads it.
///
/// This is Pylontech LV (protocol V1.2 of 2018) with the departures a Growatt SPH was found to need,
/// each marked where it is made. Every frame uses an 11-bit identifier and little-endian fields.
/// The functions are pure - numbers in, a payload out - so that they can be checked byte for byte.
namespace esphome::bms_emulator::growatt_pylontech {

/// A payload and how many of its bytes are sent.
struct Frame {
  std::array<uint8_t, 8> data{};
  uint8_t length{0};
};

static constexpr uint32_t ALIVE_ID = 0x305;
static constexpr uint32_t MANUFACTURER_ID = 0x35E;
static constexpr uint32_t REQUEST_ID = 0x35C;
static constexpr uint32_t MEASUREMENTS_ID = 0x356;
static constexpr uint32_t STATE_ID = 0x355;
static constexpr uint32_t LIMITS_ID = 0x351;
static constexpr uint32_t ALARMS_ID = 0x359;

/// The Growatt reads 0x356's temperature in hundredths of a degree, where Pylontech V1.2 says
/// tenths: sent at 0.1 degrees, 20.6 degrees read back from the inverter as 2 degrees.
static constexpr float TEMPERATURE_PER_DEGREE = 100.0f;

/// Round to the field's unit, saturating at its limits rather than wrapping.
inline int16_t to_int16(float value, float per_unit) {
  return static_cast<int16_t>(lroundf(std::clamp(value * per_unit, float(INT16_MIN), float(INT16_MAX))));
}
inline uint16_t to_uint16(float value, float per_unit) {
  return static_cast<uint16_t>(lroundf(std::clamp(value * per_unit, 0.0f, float(UINT16_MAX))));
}

inline void put_le(Frame &frame, uint8_t offset, uint16_t value) {
  frame.data[offset] = static_cast<uint8_t>(value & 0xFF);
  frame.data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

/// 0x305: a counter in byte 0 that moves every cycle. In Pylontech V1.2 0x305 is the inverter's
/// own frame; the battery side sending it is this dialect's, and the Growatt was seen not to need it.
inline Frame alive(uint8_t counter) {
  Frame frame;
  frame.length = 8;
  frame.data[0] = counter;
  return frame;
}

/// 0x35E: the manufacturer, "PYLON" padded with nulls to eight bytes.
inline Frame manufacturer() { return Frame{{'P', 'Y', 'L', 'O', 'N', 0, 0, 0}, 8}; }

/// 0x35C: charge enable in bit 7 and discharge enable in bit 6 of byte 0.
inline Frame request(bool charge_enabled, bool discharge_enabled) {
  Frame frame;
  frame.length = 2;
  frame.data[0] = (charge_enabled ? 0x80 : 0x00) | (discharge_enabled ? 0x40 : 0x00);
  return frame;
}

/// 0x356: voltage in 0.01 V, current in 0.1 A (positive while charging), temperature at
/// TEMPERATURE_PER_DEGREE.
inline Frame measurements(float volts, float amps, float degrees) {
  Frame frame;
  frame.length = 6;
  put_le(frame, 0, static_cast<uint16_t>(to_int16(volts, 100.0f)));
  put_le(frame, 2, static_cast<uint16_t>(to_int16(amps, 10.0f)));
  put_le(frame, 4, static_cast<uint16_t>(to_int16(degrees, TEMPERATURE_PER_DEGREE)));
  return frame;
}

/// 0x355: state of charge and state of health, whole percent, 0 to 100.
inline Frame state(float state_of_charge, float state_of_health) {
  Frame frame;
  frame.length = 4;
  put_le(frame, 0, to_uint16(std::clamp(state_of_charge, 0.0f, 100.0f), 1.0f));
  put_le(frame, 2, to_uint16(std::clamp(state_of_health, 0.0f, 100.0f), 1.0f));
  return frame;
}

/// 0x351: charge voltage, charge current limit, discharge current limit and discharge voltage, in
/// 0.1 V and 0.1 A. The discharge limit is given as a magnitude and sent negative, and the discharge
/// voltage fills bytes 6-7 that V1.2 leaves out: both as the Growatt has been fed.
inline Frame limits(float charge_volts, float charge_amps, float discharge_amps, float discharge_volts) {
  Frame frame;
  frame.length = 8;
  put_le(frame, 0, static_cast<uint16_t>(to_int16(charge_volts, 10.0f)));
  put_le(frame, 2, static_cast<uint16_t>(to_int16(charge_amps, 10.0f)));
  put_le(frame, 4, static_cast<uint16_t>(to_int16(-discharge_amps, 10.0f)));
  put_le(frame, 6, to_uint16(discharge_volts, 10.0f));
  return frame;
}

/// 0x359: protection and alarm flags, all clear, and the module count in byte 4. Seven bytes.
inline Frame alarms(uint8_t module_count) {
  Frame frame;
  frame.length = 7;
  frame.data[4] = module_count;
  return frame;
}

}  // namespace esphome::bms_emulator::growatt_pylontech
