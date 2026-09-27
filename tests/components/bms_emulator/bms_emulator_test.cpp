#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <gtest/gtest.h>

#include "esphome/components/bms_emulator/bms_emulator.h"
#include "esphome/components/bms_emulator/growatt_pylontech.h"
#include "esphome/components/canbus/canbus.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::bms_emulator::testing {

namespace gp = growatt_pylontech;

template<size_t N>::testing::AssertionResult frame_is(const gp::Frame &frame, const std::array<uint8_t, N> &expected) {
  if (frame.length != N)
    return ::testing::AssertionFailure() << "length " << static_cast<int>(frame.length) << ", expected " << N;
  for (size_t i = 0; i < N; i++) {
    if (frame.data[i] != expected[i]) {
      char text[40];
      snprintf(text, sizeof(text), "byte %zu is 0x%02X, expected 0x%02X", i, frame.data[i], expected[i]);
      return ::testing::AssertionFailure() << text;
    }
  }
  return ::testing::AssertionSuccess();
}

// The expected bytes were encoded with cantools 44.1 from the DBC description of this dialect - the
// encoder a Growatt SPH was fed from for months before this component existed. The values stay clear
// of exact .5 roundings, in decimal and in float arithmetic alike: -0.35f * 10 is exactly -3.5 in
// float, which lroundf() takes away from zero and the double-precision encoder did not.

TEST(GrowattPylontech, MeasurementsMatchTheDialect) {
  struct Case {
    float volts, amps, degrees;
    std::array<uint8_t, 6> expected;
  };
  // Temperature at 0.01 degrees: the Growatt reads 0x356 at that scale, not the standard's 0.1.
  const Case cases[] = {
      {53.98f, 40.53f, 21.12f, {0x16, 0x15, 0x95, 0x01, 0x40, 0x08}},
      {54.01f, -43.2f, 20.55f, {0x19, 0x15, 0x50, 0xFE, 0x07, 0x08}},
      {48.0f, 0.0f, -5.37f, {0xC0, 0x12, 0x00, 0x00, 0xE7, 0xFD}},
      {0.0f, 0.0f, 0.0f, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
      {55.37f, -0.37f, 35.9f, {0xA1, 0x15, 0xFC, 0xFF, 0x06, 0x0E}},
  };
  for (const auto &c : cases)
    EXPECT_TRUE(frame_is(gp::measurements(c.volts, c.amps, c.degrees), c.expected)) << c.volts << " V";
}

TEST(GrowattPylontech, StateIsWholePercent) {
  struct Case {
    float soc, soh;
    std::array<uint8_t, 4> expected;
  };
  const Case cases[] = {
      {79.0f, 100.0f, {0x4F, 0x00, 0x64, 0x00}},  {0.0f, 100.0f, {0x00, 0x00, 0x64, 0x00}},
      {100.0f, 100.0f, {0x64, 0x00, 0x64, 0x00}}, {85.62f, 100.0f, {0x56, 0x00, 0x64, 0x00}},
      {42.3f, 97.0f, {0x2A, 0x00, 0x61, 0x00}},
  };
  for (const auto &c : cases)
    EXPECT_TRUE(frame_is(gp::state(c.soc, c.soh), c.expected)) << c.soc << " %";
}

TEST(GrowattPylontech, LimitsSendTheDischargeLimitNegative) {
  struct Case {
    float charge_volts, charge_amps, discharge_amps, discharge_volts;
    std::array<uint8_t, 8> expected;
  };
  const Case cases[] = {
      {55.0f, 100.0f, 100.0f, 48.0f, {0x26, 0x02, 0xE8, 0x03, 0x18, 0xFC, 0xE0, 0x01}},
      {55.0f, 20.0f, 100.0f, 48.0f, {0x26, 0x02, 0xC8, 0x00, 0x18, 0xFC, 0xE0, 0x01}},
      {55.0f, 100.0f, 20.0f, 48.0f, {0x26, 0x02, 0xE8, 0x03, 0x38, 0xFF, 0xE0, 0x01}},
      {55.0f, 0.0f, 100.0f, 48.0f, {0x26, 0x02, 0x00, 0x00, 0x18, 0xFC, 0xE0, 0x01}},
      {57.6f, 150.0f, 200.0f, 44.8f, {0x40, 0x02, 0xDC, 0x05, 0x30, 0xF8, 0xC0, 0x01}},
  };
  for (const auto &c : cases) {
    EXPECT_TRUE(frame_is(gp::limits(c.charge_volts, c.charge_amps, c.discharge_amps, c.discharge_volts), c.expected))
        << c.charge_amps << " A / " << c.discharge_amps << " A";
  }
}

TEST(GrowattPylontech, RequestCarriesBothEnables) {
  EXPECT_TRUE(frame_is(gp::request(true, true), std::array<uint8_t, 2>{0xC0, 0x00}));
  EXPECT_TRUE(frame_is(gp::request(false, true), std::array<uint8_t, 2>{0x40, 0x00}));
  EXPECT_TRUE(frame_is(gp::request(true, false), std::array<uint8_t, 2>{0x80, 0x00}));
  EXPECT_TRUE(frame_is(gp::request(false, false), std::array<uint8_t, 2>{0x00, 0x00}));
}

TEST(GrowattPylontech, AlarmsAreClearWithTheModuleCount) {
  EXPECT_TRUE(frame_is(gp::alarms(1), std::array<uint8_t, 7>{0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}));
  EXPECT_TRUE(frame_is(gp::alarms(2), std::array<uint8_t, 7>{0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00}));
}

TEST(GrowattPylontech, AliveAndManufacturer) {
  EXPECT_TRUE(frame_is(gp::alive(1), std::array<uint8_t, 8>{0x01, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_TRUE(frame_is(gp::alive(255), std::array<uint8_t, 8>{0xFF, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_TRUE(frame_is(gp::manufacturer(), std::array<uint8_t, 8>{'P', 'Y', 'L', 'O', 'N', 0, 0, 0}));
}

TEST(GrowattPylontech, ValuesPastAFieldSaturateRatherThanWrap) {
  // 400 V at 0.01 V is 40000, past a signed 16-bit field; wrapping would read as a negative voltage.
  EXPECT_TRUE(
      frame_is(gp::measurements(400.0f, -4000.0f, 400.0f), std::array<uint8_t, 6>{0xFF, 0x7F, 0x00, 0x80, 0xFF, 0x7F}));
  EXPECT_TRUE(frame_is(gp::state(120.0f, -3.0f), std::array<uint8_t, 4>{0x64, 0x00, 0x00, 0x00}));
  EXPECT_TRUE(frame_is(gp::limits(4000.0f, 4000.0f, 4000.0f, -1.0f),
                       std::array<uint8_t, 8>{0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x80, 0x00, 0x00}));
}

// The component, at the CAN frame boundary: values in, frames out.

class RecordingCanbus : public canbus::Canbus {
 public:
  struct Sent {
    uint32_t can_id;
    bool extended;
    std::vector<uint8_t> data;
  };
  std::vector<Sent> sent;

 protected:
  bool setup_internal() override { return true; }
  canbus::Error send_message(struct canbus::CanFrame *frame) override {
    this->sent.push_back({frame->can_id, frame->use_extended_id,
                          std::vector<uint8_t>(frame->data, frame->data + frame->can_data_length_code)});
    return canbus::ERROR_OK;
  }
  canbus::Error read_message(struct canbus::CanFrame *frame) override { return canbus::ERROR_NOMSG; }
};

class BmsEmulatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->emulator_.set_voltage_sensor(&this->voltage_);
    this->emulator_.set_current_sensor(&this->current_);
    this->emulator_.set_state_of_charge_sensor(&this->state_of_charge_);
    this->emulator_.set_temperature_sensor(&this->temperature_);
    this->emulator_.set_state_of_health(100.0f);
    this->emulator_.set_charge_voltage(55.0f);
    this->emulator_.set_discharge_voltage(48.0f);
    this->emulator_.set_charge_current_limit(100.0f);
    this->emulator_.set_discharge_current_limit(100.0f);
    this->emulator_.set_charge_enabled(true);
    this->emulator_.set_discharge_enabled(true);
  }

  void give_battery_data() {
    this->voltage_.state = 53.98f;
    this->current_.state = 40.53f;
    this->state_of_charge_.state = 79.0f;
    this->temperature_.state = 21.12f;
  }

  RecordingCanbus canbus_;
  sensor::Sensor voltage_, current_, state_of_charge_, temperature_;
  BmsEmulator emulator_{&this->canbus_, BmsEmulatorType::BMS_EMULATOR_TYPE_GROWATT_PYLONTECH};
};

TEST_F(BmsEmulatorTest, SendsTheSevenFramesInTheOrderTheInverterIsUsedTo) {
  this->give_battery_data();
  this->emulator_.update();

  const std::vector<std::pair<uint32_t, size_t>> expected = {{0x305, 8}, {0x35E, 8}, {0x35C, 2}, {0x356, 6},
                                                             {0x355, 4}, {0x351, 8}, {0x359, 7}};
  ASSERT_EQ(this->canbus_.sent.size(), expected.size());
  for (size_t i = 0; i < expected.size(); i++) {
    EXPECT_EQ(this->canbus_.sent[i].can_id, expected[i].first) << "frame " << i;
    EXPECT_FALSE(this->canbus_.sent[i].extended) << "frame " << i;
    EXPECT_EQ(this->canbus_.sent[i].data.size(), expected[i].second) << "frame " << i;
  }
  EXPECT_EQ(this->canbus_.sent[3].data, (std::vector<uint8_t>{0x16, 0x15, 0x95, 0x01, 0x40, 0x08}));
  EXPECT_EQ(this->canbus_.sent[5].data, (std::vector<uint8_t>{0x26, 0x02, 0xE8, 0x03, 0x18, 0xFC, 0xE0, 0x01}));
}

TEST_F(BmsEmulatorTest, SendsNothingWhileAMeasurementHasNoValue) {
  this->give_battery_data();
  this->temperature_.state = NAN;
  this->emulator_.update();
  EXPECT_TRUE(this->canbus_.sent.empty());

  // And nothing before any value has arrived at all.
  RecordingCanbus other;
  BmsEmulator fresh{&other, BmsEmulatorType::BMS_EMULATOR_TYPE_GROWATT_PYLONTECH};
  sensor::Sensor v, i, s, t;
  fresh.set_voltage_sensor(&v);
  fresh.set_current_sensor(&i);
  fresh.set_state_of_charge_sensor(&s);
  fresh.set_temperature_sensor(&t);
  fresh.update();
  EXPECT_TRUE(other.sent.empty());
}

TEST_F(BmsEmulatorTest, FeedsAgainOnceTheMissingValueReturns) {
  this->give_battery_data();
  this->current_.state = NAN;
  this->emulator_.update();
  this->current_.state = -3.5f;
  this->emulator_.update();
  EXPECT_EQ(this->canbus_.sent.size(), 7u);
}

TEST_F(BmsEmulatorTest, AliveCounterMovesEveryCycleAndWraps) {
  this->give_battery_data();
  for (int cycle = 1; cycle <= 256; cycle++) {
    this->canbus_.sent.clear();
    this->emulator_.update();
    ASSERT_FALSE(this->canbus_.sent.empty());
    EXPECT_EQ(this->canbus_.sent[0].data[0], static_cast<uint8_t>(cycle)) << "cycle " << cycle;
  }
}

TEST_F(BmsEmulatorTest, ChargeDisabledClearsItsRequestBit) {
  this->give_battery_data();
  this->emulator_.set_charge_enabled(false);
  this->emulator_.update();
  EXPECT_EQ(this->canbus_.sent[2].data, (std::vector<uint8_t>{0x40, 0x00}));
}

TEST_F(BmsEmulatorTest, InverterIsOnlineWhileItsFrameKeepsArriving) {
  EXPECT_FALSE(this->emulator_.is_inverter_online());
  this->emulator_.on_frame(0x301, false, false, 1000);
  EXPECT_TRUE(this->emulator_.is_inverter_online());
  this->emulator_.check_inverter(1000 + 4999);
  EXPECT_TRUE(this->emulator_.is_inverter_online());
  this->emulator_.check_inverter(1000 + 5000);
  EXPECT_FALSE(this->emulator_.is_inverter_online());
}

TEST_F(BmsEmulatorTest, OnlyTheInvertersOwnFrameCounts) {
  this->emulator_.on_frame(0x301, true, false, 1000);  // an extended identifier is another frame altogether
  this->emulator_.on_frame(0x302, false, false, 1000);
  this->emulator_.on_frame(0x301, false, true, 1000);  // a remote request carries nothing from the inverter
  EXPECT_FALSE(this->emulator_.is_inverter_online());

  this->emulator_.set_inverter_frame_id(0x305);
  this->emulator_.on_frame(0x305, false, false, 1000);
  EXPECT_TRUE(this->emulator_.is_inverter_online());
}

}  // namespace esphome::bms_emulator::testing
