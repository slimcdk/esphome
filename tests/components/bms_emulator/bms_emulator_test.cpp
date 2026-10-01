#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/bms_emulator/bms_emulator.h"
#include "esphome/components/bms_emulator/growatt_pylontech.h"
#include "esphome/components/canbus/canbus.h"
#include "esphome/components/logger/logger.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/application.h"

#ifndef USE_BINARY_SENSOR
#error "the host build must include binary_sensor; see __init__.py beside this file"
#endif

namespace esphome::bms_emulator::testing {

using growatt_pylontech::Frame;

template<size_t N>::testing::AssertionResult frame_is(const Frame &frame, const std::array<uint8_t, N> &expected) {
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

// The expected bytes were encoded independently, with cantools from a DBC description of this frame
// set; the last cases in a table are the ends of each field's range there. The values stay clear of
// exact .5 roundings: -0.35f * 10 is exactly -3.5 in float, which lroundf() takes away from zero.

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
      {327.67f, 250.0f, 75.0f, {0xFF, 0x7F, 0xC4, 0x09, 0x4C, 0x1D}},
      {0.0f, -250.0f, -50.0f, {0x00, 0x00, 0x3C, 0xF6, 0x78, 0xEC}},
  };
  for (const auto &c : cases) {
    EXPECT_TRUE(frame_is(growatt_pylontech::measurements(c.volts, c.amps, c.degrees), c.expected)) << c.volts << " V";
  }
}

TEST(GrowattPylontech, StateIsWholePercent) {
  struct Case {
    float state_of_charge, state_of_health;
    std::array<uint8_t, 4> expected;
  };
  const Case cases[] = {
      {79.0f, 100.0f, {0x4F, 0x00, 0x64, 0x00}},  {0.0f, 100.0f, {0x00, 0x00, 0x64, 0x00}},
      {100.0f, 100.0f, {0x64, 0x00, 0x64, 0x00}}, {85.62f, 100.0f, {0x56, 0x00, 0x64, 0x00}},
      {42.3f, 97.0f, {0x2A, 0x00, 0x61, 0x00}},
  };
  for (const auto &c : cases) {
    EXPECT_TRUE(frame_is(growatt_pylontech::state(c.state_of_charge, c.state_of_health), c.expected))
        << c.state_of_charge << " %";
  }
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
      {75.0f, 500.0f, 500.0f, 6553.5f, {0xEE, 0x02, 0x88, 0x13, 0x78, 0xEC, 0xFF, 0xFF}},
      {0.0f, 0.0f, 0.0f, 0.0f, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
  };
  for (const auto &c : cases) {
    EXPECT_TRUE(frame_is(growatt_pylontech::limits(c.charge_volts, c.charge_amps, c.discharge_amps, c.discharge_volts),
                         c.expected))
        << c.charge_amps << " A / " << c.discharge_amps << " A";
  }
}

TEST(GrowattPylontech, RequestCarriesBothEnables) {
  EXPECT_TRUE(frame_is(growatt_pylontech::request(true, true), std::array<uint8_t, 2>{0xC0, 0x00}));
  EXPECT_TRUE(frame_is(growatt_pylontech::request(false, true), std::array<uint8_t, 2>{0x40, 0x00}));
  EXPECT_TRUE(frame_is(growatt_pylontech::request(true, false), std::array<uint8_t, 2>{0x80, 0x00}));
  EXPECT_TRUE(frame_is(growatt_pylontech::request(false, false), std::array<uint8_t, 2>{0x00, 0x00}));
}

TEST(GrowattPylontech, AlarmsAreClearWithTheModuleCount) {
  EXPECT_TRUE(frame_is(growatt_pylontech::alarms(1), std::array<uint8_t, 7>{0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}));
  EXPECT_TRUE(frame_is(growatt_pylontech::alarms(2), std::array<uint8_t, 7>{0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00}));
}

TEST(GrowattPylontech, AliveAndManufacturer) {
  EXPECT_TRUE(frame_is(growatt_pylontech::alive(1), std::array<uint8_t, 8>{0x01, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_TRUE(frame_is(growatt_pylontech::alive(255), std::array<uint8_t, 8>{0xFF, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_TRUE(frame_is(growatt_pylontech::manufacturer(), std::array<uint8_t, 8>{'P', 'Y', 'L', 'O', 'N', 0, 0, 0}));
}

TEST(GrowattPylontech, ValuesPastAFieldSaturateRatherThanWrap) {
  // 400 V at 0.01 V is 40000, past a signed 16-bit field; wrapping would read as a negative voltage.
  EXPECT_TRUE(frame_is(growatt_pylontech::measurements(400.0f, -4000.0f, 400.0f),
                       std::array<uint8_t, 6>{0xFF, 0x7F, 0x00, 0x80, 0xFF, 0x7F}));
  EXPECT_TRUE(frame_is(growatt_pylontech::state(120.0f, -3.0f), std::array<uint8_t, 4>{0x64, 0x00, 0x00, 0x00}));
  EXPECT_TRUE(frame_is(growatt_pylontech::limits(4000.0f, 4000.0f, 4000.0f, -1.0f),
                       std::array<uint8_t, 8>{0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x80, 0x00, 0x00}));
}

// The component, at the CAN frame boundary: values and received frames in, frames and entity
// state out.

class RecordingCanbus : public canbus::Canbus {
 public:
  struct Sent {
    uint32_t can_id;
    bool extended;
    std::vector<uint8_t> data;
  };
  std::vector<Sent> sent;
  /// The send attempt, counted from zero over the whole test, that the controller refuses.
  int refuse_attempt{-1};

  /// Queue a frame for loop() to deliver, as a controller hands out what it has received.
  void receive(uint32_t can_id, bool extended_id, bool rtr) {
    canbus::CanFrame frame{};
    frame.can_id = can_id;
    frame.use_extended_id = extended_id;
    frame.remote_transmission_request = rtr;
    frame.can_data_length_code = 8;
    this->incoming_.push_back(frame);
  }

 protected:
  bool setup_internal() override { return true; }
  canbus::Error send_message(struct canbus::CanFrame *frame) override {
    if (this->attempts_++ == this->refuse_attempt)
      return canbus::ERROR_FAILTX;
    this->sent.push_back({frame->can_id, frame->use_extended_id,
                          std::vector<uint8_t>(frame->data, frame->data + frame->can_data_length_code)});
    return canbus::ERROR_OK;
  }
  canbus::Error read_message(struct canbus::CanFrame *frame) override {
    if (this->incoming_.empty())
      return canbus::ERROR_NOMSG;
    *frame = this->incoming_.front();
    this->incoming_.erase(this->incoming_.begin());
    return canbus::ERROR_OK;
  }

 private:
  std::vector<canbus::CanFrame> incoming_;
  int attempts_{0};
};

/// Every line the component logged, for the whole run: a log callback cannot be taken off again, so
/// registering one per test would leave the logger calling into objects that no longer exist.
std::vector<std::string> &emulator_log() {
  static std::vector<std::string> lines;
  static const bool registered = []() {
    if (logger::global_logger == nullptr)
      return false;
    logger::global_logger->add_log_callback(
        &lines, [](void *self, uint8_t, const char *tag, const char *message, size_t length) {
          if (strcmp(tag, "bms_emulator") == 0)
            static_cast<std::vector<std::string> *>(self)->emplace_back(message, length);
        });
    return true;
  }();
  EXPECT_TRUE(registered) << "no logger to listen to";
  return lines;
}

size_t lines_containing(const std::vector<std::string> &lines, const char *text) {
  return std::count_if(lines.begin(), lines.end(),
                       [text](const std::string &line) { return line.find(text) != std::string::npos; });
}

class BmsEmulatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
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

  /// The frame with this identifier from the last cycle sent.
  const std::vector<uint8_t> &last(uint32_t can_id) {
    auto it = std::find_if(this->canbus_.sent.rbegin(), this->canbus_.sent.rend(),
                           [can_id](const RecordingCanbus::Sent &frame) { return frame.can_id == can_id; });
    EXPECT_NE(it, this->canbus_.sent.rend()) << "no frame 0x" << std::hex << can_id;
    static const std::vector<uint8_t> none;
    return it == this->canbus_.sent.rend() ? none : it->data;
  }

  RecordingCanbus canbus_;
  sensor::Sensor voltage_, current_, state_of_charge_, temperature_;
  BmsEmulator emulator_{&this->canbus_,          BmsEmulatorType::BMS_EMULATOR_TYPE_GROWATT_PYLONTECH,
                        &this->voltage_,         &this->current_,
                        &this->state_of_charge_, &this->temperature_};
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
  EXPECT_EQ(this->last(0x356), (std::vector<uint8_t>{0x16, 0x15, 0x95, 0x01, 0x40, 0x08}));
  EXPECT_EQ(this->last(0x351), (std::vector<uint8_t>{0x26, 0x02, 0xE8, 0x03, 0x18, 0xFC, 0xE0, 0x01}));
}

TEST_F(BmsEmulatorTest, SendsNothingWhileAMeasurementHasNoValue) {
  this->give_battery_data();
  this->temperature_.state = NAN;
  this->emulator_.update();
  EXPECT_TRUE(this->canbus_.sent.empty());

  // And nothing before any value has arrived at all.
  RecordingCanbus other;
  sensor::Sensor voltage, current, state_of_charge, temperature;
  BmsEmulator fresh{
      &other, BmsEmulatorType::BMS_EMULATOR_TYPE_GROWATT_PYLONTECH, &voltage, &current, &state_of_charge, &temperature};
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

// A limit is what the inverter charges and discharges by. One without a value - a lambda reading a
// sensor that has not answered - is no more a reason to guess than a missing measurement is.
TEST_F(BmsEmulatorTest, SendsNothingWhileALimitHasNoValue) {
  this->give_battery_data();
  this->emulator_.set_charge_current_limit(NAN);
  this->emulator_.update();
  EXPECT_TRUE(this->canbus_.sent.empty());

  this->emulator_.set_charge_current_limit(100.0f);
  this->emulator_.set_discharge_voltage([]() -> float { return NAN; });
  this->emulator_.update();
  EXPECT_TRUE(this->canbus_.sent.empty());

  this->emulator_.set_discharge_voltage(48.0f);
  this->emulator_.update();
  EXPECT_EQ(this->canbus_.sent.size(), 7u);
}

// State of health is not a limit: the inverter only displays it, and most batteries do not report
// one. A missing value is sent as a healthy battery rather than costing the inverter its BMS.
TEST_F(BmsEmulatorTest, StateOfHealthWithoutAValueIsSentAsFull) {
  this->give_battery_data();
  this->emulator_.set_state_of_health([]() -> float { return 97.0f; });
  this->emulator_.update();
  EXPECT_EQ(this->last(0x355), (std::vector<uint8_t>{0x4F, 0x00, 0x61, 0x00}));

  this->emulator_.set_state_of_health(NAN);
  this->emulator_.update();
  EXPECT_EQ(this->canbus_.sent.size(), 14u);
  EXPECT_EQ(this->last(0x355), (std::vector<uint8_t>{0x4F, 0x00, 0x64, 0x00}));
}

TEST_F(BmsEmulatorTest, ChargeDisabledClearsItsRequestBit) {
  this->give_battery_data();
  this->emulator_.set_charge_enabled(false);
  this->emulator_.update();
  EXPECT_EQ(this->last(0x35C), (std::vector<uint8_t>{0x40, 0x00}));
}

// A stop is said twice, as the request bit above and as a charge limit of nothing here.
TEST_F(BmsEmulatorTest, AChargeLimitOfNothingIsSentAsZero) {
  this->give_battery_data();
  this->emulator_.set_charge_current_limit(0.0f);
  this->emulator_.update();
  EXPECT_EQ(this->last(0x351), (std::vector<uint8_t>{0x26, 0x02, 0x00, 0x00, 0x18, 0xFC, 0xE0, 0x01}));
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

// A frame the controller refuses means its transmit queue is full - no inverter acknowledging, for
// one. Each further send would wait out the controller's timeout too, so the cycle stops there.
TEST_F(BmsEmulatorTest, AFrameTheControllerRefusesEndsTheCycle) {
  this->give_battery_data();
  this->canbus_.refuse_attempt = 2;
  this->emulator_.update();
  ASSERT_EQ(this->canbus_.sent.size(), 2u);
  EXPECT_EQ(this->canbus_.sent[1].can_id, 0x35Eu);

  this->emulator_.update();
  ASSERT_EQ(this->canbus_.sent.size(), 9u) << "the next cycle sends the whole set again";
  EXPECT_EQ(this->canbus_.sent[2].can_id, 0x305u);
  EXPECT_EQ(this->canbus_.sent[2].data[0], 2) << "the counter still moves once a cycle";
}

TEST_F(BmsEmulatorTest, SaysOnceEachWayWhetherTheInverterIsFed) {
  auto &log = emulator_log();
  log.clear();
  this->give_battery_data();
  this->emulator_.update();
  this->emulator_.update();
  this->temperature_.state = NAN;
  this->emulator_.update();
  this->emulator_.update();
  this->temperature_.state = 21.12f;
  this->emulator_.update();

  EXPECT_EQ(lines_containing(log, "feeding the inverter"), 2u);
  EXPECT_EQ(lines_containing(log, "is not being fed"), 1u);
}

// The inverter's frame reaches the emulator the way it does on a device: received by the bus,
// handed to the callback setup() registered.
TEST_F(BmsEmulatorTest, InverterIsOnlineWhileItsFrameKeepsArriving) {
  binary_sensor::BinarySensor online;
  this->emulator_.set_inverter_online_binary_sensor(&online);
  this->emulator_.setup();
  EXPECT_TRUE(online.has_state());
  EXPECT_FALSE(online.state);

  const uint32_t heard_at = App.get_loop_component_start_time();
  this->canbus_.receive(0x301, false, false);
  this->canbus_.loop();
  EXPECT_TRUE(this->emulator_.is_inverter_online());
  EXPECT_TRUE(online.state);

  this->emulator_.check_inverter(heard_at + 4999);
  EXPECT_TRUE(online.state);
  this->emulator_.check_inverter(heard_at + 5000);
  EXPECT_FALSE(this->emulator_.is_inverter_online());
  EXPECT_FALSE(online.state);
}

TEST_F(BmsEmulatorTest, OnlyTheInvertersOwnFrameCounts) {
  this->emulator_.setup();
  this->canbus_.receive(0x301, true, false);  // an extended identifier is another frame altogether
  this->canbus_.receive(0x302, false, false);
  this->canbus_.receive(0x301, false, true);  // a remote request carries nothing from the inverter
  this->canbus_.loop();
  EXPECT_FALSE(this->emulator_.is_inverter_online());

  this->emulator_.set_inverter_frame_id(0x305);
  this->canbus_.receive(0x305, false, false);
  this->canbus_.loop();
  EXPECT_TRUE(this->emulator_.is_inverter_online());
}

}  // namespace esphome::bms_emulator::testing
