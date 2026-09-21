#include "common.h"

namespace esphome::masterbus::testing {

// Two kinds of data appear below. Frames written as a line of log output were recorded off a live
// installation and are quoted as they were logged, with the capture line beside them; every other
// payload is built to the shape the protocol notes describe, to exercise one decision at a time.
//
// The addresses are recorded. 0x6D56EA and 0x6C4ECB are two of the battery blocks the vendor
// library reports by the same number, which is what pins the low 24 bits of the identifier as the
// device address.
static constexpr uint32_t BATTERY_1 = 0x6D56EA;
static constexpr uint32_t BATTERY_6 = 0x6C4ECB;
static constexpr uint32_t UNDECLARED_DEVICE = 0x28B289;

class MasterbusTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->hub_ = std::make_unique<MasterbusHub>(&this->canbus_);
    this->battery_ = std::make_unique<MasterbusDevice>(this->hub_.get(), BATTERY_1);
    this->hub_->register_device(this->battery_.get());
  }

  /// A second device on the same hub, for the questions that are about telling them apart.
  MasterbusDevice *add_device(uint32_t address) {
    auto device = std::make_unique<MasterbusDevice>(this->hub_.get(), address);
    auto *raw = device.get();
    this->hub_->register_device(raw);
    this->devices_.push_back(std::move(device));
    return raw;
  }

  RecordingEntity *add_sensor(uint16_t param, MasterbusValueType type = MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT,
                              MasterbusTab tab = MasterbusTab::MASTERBUS_TAB_MONITORING,
                              MasterbusDevice *device = nullptr) {
    auto entity =
        std::make_unique<RecordingEntity>(device != nullptr ? device : this->battery_.get(), param, tab, type);
    auto *raw = entity.get();
    this->hub_->register_entity(raw);
    this->entities_.push_back(std::move(entity));
    return raw;
  }

  /// Feed a frame exactly as log_all_frames printed it.
  void feed(const char *logged) {
    const LoggedFrame frame = logged_frame(logged);
    this->hub_->on_frame(frame.can_id, frame.extended_id, frame.rtr, frame.data);
  }

  /// Let the walk send its next question, then hand it the answer.
  void answer(uint8_t type, const std::vector<uint8_t> &data) {
    this->hub_->scan_step(this->scan_now_);
    this->hub_->on_frame(frame_id(type, BATTERY_1), true, false, data);
  }

  /// Let the walk send its next question and hear nothing back, which is how it learns that a
  /// list has ended or that a field does not carry a property.
  void unanswered() {
    this->hub_->scan_step(this->scan_now_);
    this->scan_now_ += SCAN_ANSWER_TIMEOUT_MS;
    this->hub_->scan_step(this->scan_now_);
  }

  /// The walk's own clock, moved on only by a question nobody answers.
  uint32_t scan_now_{0};
  RecordingCanbus canbus_;
  std::unique_ptr<MasterbusHub> hub_;
  std::unique_ptr<MasterbusDevice> battery_;
  std::vector<std::unique_ptr<MasterbusDevice>> devices_;
  std::vector<std::unique_ptr<RecordingEntity>> entities_;
};

TEST_F(MasterbusTest, MonitoringAnswerUpdatesTheMatchingSensor) {
  auto *voltage = this->add_sensor(1);
  auto *current = this->add_sensor(2);

  // Recorded: can_mb 086D56EA#0100 92EDD141 -> 26.241 V on field 1. 0x086D56EA is the identifier
  // as it appears on the wire for a monitoring answer from BAT 1.
  ASSERT_EQ(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), 0x086D56EAu);
  this->feed("ext 0x086D56EA [6] 01:00:92:ED:D1:41");

  ASSERT_EQ(voltage->values.size(), 1u);
  EXPECT_FLOAT_EQ(voltage->values[0].as_float, 26.241f);
  EXPECT_TRUE(current->values.empty());
}

TEST_F(MasterbusTest, AnswerForAnotherDeviceIsIgnored) {
  auto *voltage = this->add_sensor(1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_6), true, false, monitoring_answer(1, 26.241f));

  EXPECT_TRUE(voltage->values.empty());
}

TEST_F(MasterbusTest, RequestFrameIsNotMistakenForAnAnswer) {
  auto *voltage = this->add_sensor(1);

  // A request carries the field number and nothing else. Reading it as an answer would publish
  // whatever followed in memory.
  this->hub_->on_frame(frame_id(MONITORING_REQUEST_TYPE, BATTERY_1), true, false, {0x01, 0x00});

  EXPECT_TRUE(voltage->values.empty());
}

TEST_F(MasterbusTest, ShortPayloadIsDropped) {
  auto *voltage = this->add_sensor(1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, {0x01, 0x00, 0x92, 0xED});

  EXPECT_TRUE(voltage->values.empty());
}

TEST_F(MasterbusTest, StandardIdentifierIsIgnored) {
  auto *voltage = this->add_sensor(1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), false, false, monitoring_answer(1, 26.241f));

  EXPECT_TRUE(voltage->values.empty());
}

TEST_F(MasterbusTest, BooleanFieldReadsAsBoolean) {
  // A boolean field arrives as a float 0.0 or 1.0, and declaring it boolean is what turns that
  // into a state rather than a number.
  auto *relay = this->add_sensor(117, MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(117, 1.0f));

  ASSERT_EQ(relay->values.size(), 1u);
  EXPECT_EQ(relay->values[0].type, MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);
  EXPECT_TRUE(relay->values[0].as_boolean);
}

TEST_F(MasterbusTest, NotANumberReportsUnavailableRatherThanZero) {
  auto *voltage = this->add_sensor(1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, NAN));

  EXPECT_TRUE(voltage->values.empty());
  EXPECT_EQ(voltage->unavailable_count, 1);
}

// A text field answers with the number of an entry in the device's string table rather than with
// text, so publishing one takes a second exchange. Frames below are shaped the way the bus shapes
// them; string 97 is the "Battery" the scan tests read through the same table.
TEST_F(MasterbusTest, TextFieldIsReadFromTheStringTable) {
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));

  // Nothing is published yet; what went out is a request for the first chunk of string 97.
  EXPECT_TRUE(label->values.empty());
  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  const auto &request = this->canbus_.sent[0];
  EXPECT_EQ(request.can_id >> MESSAGE_TYPE_SHIFT, STRING_REQUEST_TYPE);
  EXPECT_EQ(request.can_id & DEVICE_ADDRESS_MASK, BATTERY_1);
  ASSERT_EQ(request.can_data_length_code, 4);
  EXPECT_EQ(request.data[0], STRING_REQUEST_MARKER);
  EXPECT_EQ(encode_uint16(request.data[2], request.data[1]), 97);
  EXPECT_EQ(request.data[3], 0);

  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});

  ASSERT_EQ(label->values.size(), 1u);
  EXPECT_EQ(label->values[0].type, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);
  EXPECT_STREQ(label->values[0].as_text, "Battery");
}

TEST_F(MasterbusTest, TextReadIgnoresAChunkOfSomeOtherString) {
  // The same mistake the scan makes if it trusts a counter instead of the header: a chunk of
  // string 98 is not the next piece of string 97.
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x62, 0x00, 0x00, 'Z', 'Z', 'Z', 'Z'});
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});

  ASSERT_EQ(label->values.size(), 1u);
  EXPECT_STREQ(label->values[0].as_text, "Battery");
}

TEST_F(MasterbusTest, TextFieldWithNoStringIsUnavailableAndAsksNothing) {
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 0.0f));

  EXPECT_TRUE(label->values.empty());
  EXPECT_EQ(label->unavailable_count, 1);
  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, StringTheDeviceDoesNotHaveLeavesTheFieldUnavailable) {
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false, {0x30, 0x61, 0x00, 0x00});

  EXPECT_TRUE(label->values.empty());
  EXPECT_EQ(label->unavailable_count, 1);
}

// Whose answer a value is decides whether this entity asks again: an answer to our own question
// says nothing about whether anybody else is covering the field, and one from elsewhere does.
TEST_F(MasterbusTest, APollTheBusRefusedDoesNotMakeSomebodyElsesAnswerOurs) {
  auto *voltage = this->add_sensor(1);
  voltage->set_update_interval(10000);

  this->canbus_.error = canbus::ERROR_ALLTXBUSY;
  voltage->update();
  ASSERT_TRUE(this->canbus_.sent.empty());
  this->canbus_.error = canbus::ERROR_OK;

  // Ours never went out, so this one is somebody else's, and the field is covered without us.
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  voltage->update();

  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, ATextAnswerCountsAsAnAnswerBeforeItsTextArrives) {
  // The monitoring answer came from somebody else's request, so there is nothing to gain by
  // asking again - even though the text it points at has not been read yet.
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);
  label->set_update_interval(10000);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  this->canbus_.clear();

  label->update();

  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, TimeFieldReadsAsSecondsSinceMidnight) {
  auto *clock = this->add_sensor(94, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(94, 45296.0f));

  ASSERT_EQ(clock->values.size(), 1u);
  EXPECT_EQ(clock->values[0].type, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);
  EXPECT_STREQ(clock->values[0].as_text, "12:34:56");
}

TEST_F(MasterbusTest, DateFieldUnpacksTheCalendarTheDevicePacked) {
  // 841202 is what a Mastervolt DC shunt reported on 2022-01-18, taken from a capture of an
  // unrelated installation. It is the whole of the evidence that months hold 32 days here.
  auto *today = this->add_sensor(95, MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(95, 841202.0f));

  ASSERT_EQ(today->values.size(), 1u);
  EXPECT_EQ(today->values[0].type, MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE);
  EXPECT_STREQ(today->values[0].as_text, "2022-01-18");
}

TEST_F(MasterbusTest, TimeKeepsItsLeadingZeroes) {
  auto *clock = this->add_sensor(94, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  // A minute and a second past midnight.
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(94, 61.0f));

  ASSERT_EQ(clock->values.size(), 1u);
  EXPECT_STREQ(clock->values[0].as_text, "00:01:01");
}

TEST_F(MasterbusTest, DateKeepsItsLeadingZeroes) {
  auto *today = this->add_sensor(95, MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(95, 841185.0f));

  ASSERT_EQ(today->values.size(), 1u);
  EXPECT_STREQ(today->values[0].as_text, "2022-01-01");
}

TEST_F(MasterbusTest, ATimeSpanIsNotFoldedIntoADay) {
  // The same display type serves a clock and a countdown. A DC shunt reporting hours of charge
  // left answered 579120 on the bus this was captured from; folding that into a day would report
  // a plausible and wrong time of day.
  auto *remaining = this->add_sensor(4, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(4, 579120.0f));

  ASSERT_EQ(remaining->values.size(), 1u);
  EXPECT_STREQ(remaining->values[0].as_text, "160:52:00");
}

TEST_F(MasterbusTest, ANumberThatCannotBeATimeIsUnavailableRatherThanTruncated) {
  // Casting a float outside the destination range is undefined, so a field declared as a time
  // that answers with something else is refused rather than truncated into a plausible one.
  auto *clock = this->add_sensor(94, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(94, -1.0f));
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false,
                       monitoring_answer(94, 235929600.0f));

  EXPECT_TRUE(clock->values.empty());
  EXPECT_EQ(clock->unavailable_count, 2);
}

// A number that publishes as text is rendered somewhere, and a string arrives in chunks across
// several frames. Rendering one where the other is being assembled loses the string.
TEST_F(MasterbusTest, ATimeAnswerDoesNotOverwriteAStringBeingRead) {
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);
  auto *clock = this->add_sensor(94, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(94, 45296.0f));
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});

  ASSERT_EQ(label->values.size(), 1u);
  EXPECT_STREQ(label->values[0].as_text, "Battery");
  ASSERT_EQ(clock->values.size(), 1u);
  EXPECT_STREQ(clock->values[0].as_text, "12:34:56");
}

// The same collision where the string ends on an empty chunk: the terminator lands past what the
// number wrote, so the field publishes the number's leading digits as its text.
TEST_F(MasterbusTest, ATimeAnswerDoesNotTruncateAStringThatEndsOnAnEmptyChunk) {
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);
  this->add_sensor(94, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(94, 45296.0f));
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false, {0x30, 0x61, 0x00, 0x01, 0x00});

  ASSERT_EQ(label->values.size(), 1u);
  EXPECT_STREQ(label->values[0].as_text, "Batt");
}

TEST_F(MasterbusTest, AFrameTheDeviceSentMarksItOnline) {
  EXPECT_FALSE(this->battery_->is_online());

  // An announcement says nothing about any field, but the device sent it.
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});

  EXPECT_TRUE(this->battery_->is_online());
}

// A request carries the address of the device it is aimed at, never its sender's. A display panel
// polling a device that died would otherwise report it alive for as long as the panel kept asking.
TEST_F(MasterbusTest, AFrameAddressedToTheDeviceDoesNotProveItIsThere) {
  this->hub_->on_frame(frame_id(MONITORING_REQUEST_TYPE, BATTERY_1), true, false, {0x01, 0x00});
  EXPECT_FALSE(this->battery_->is_online());

  // Nor does a type nobody has decoded: it may be somebody else's question.
  this->hub_->on_frame(frame_id(0x04, BATTERY_1), true, false, {0x1B, 0xEA, 0x56});
  EXPECT_FALSE(this->battery_->is_online());
}

// Which half of each family a frame belongs to is the whole of the rule above: answers and
// refusals come back from the device, requests go out to it.
TEST_F(MasterbusTest, OnlyTheHalfOfEachFamilyTheDeviceSendsCounts) {
  for (uint8_t message = 0; message < MESSAGE_COUNT; message++) {
    EXPECT_TRUE(device_sent_message(MESSAGE_INFORMATION_BASE + message)) << "information " << +message;
    EXPECT_TRUE(device_sent_message(MESSAGE_NOT_AVAILABLE_BASE + message)) << "refusal " << +message;
    EXPECT_FALSE(device_sent_message(MESSAGE_REQUEST_BASE + message)) << "request " << +message;
  }
  for (uint8_t type :
       {DEVICE_ANNOUNCEMENT_TYPE, NODE_NOT_AVAILABLE_TYPE, STRING_INFORMATION_TYPE, STRING_NOT_AVAILABLE_TYPE})
    EXPECT_TRUE(device_sent_message(type)) << "type " << +type;
  for (uint8_t type : {NODE_REQUEST_TYPE, STRING_REQUEST_TYPE})
    EXPECT_FALSE(device_sent_message(type)) << "type " << +type;

  // A type nobody has decoded may be somebody's question, so it proves nothing either.
  EXPECT_FALSE(device_sent_message(0x04));
}

TEST_F(MasterbusTest, ADeviceIsJudgedTimedOutOnlyOnceItsTimeoutHasRun) {
  this->battery_->set_timeout(60000);
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  const uint32_t answered_at = App.get_loop_component_start_time();

  EXPECT_FALSE(this->battery_->is_timed_out(answered_at + 59999));
  EXPECT_TRUE(this->battery_->is_timed_out(answered_at + 60000));

  // A returning device is judged from its own answer, not from the first one.
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.3f));
  const uint32_t answered_again_at = App.get_loop_component_start_time();
  EXPECT_FALSE(this->battery_->is_timed_out(answered_again_at + 59999));
  EXPECT_TRUE(this->battery_->is_timed_out(answered_again_at + 60000));
}

// Field rates vary enormously within one device - a setting is read once an hour beside a current
// read every second - so availability belongs to the device: when it goes quiet everything it
// carries goes with it, whatever each field's own cadence was.
TEST_F(MasterbusTest, ADeviceGoingQuietTakesAllItsEntitiesDownTogetherAndComesBackWithThem) {
  this->battery_->set_timeout(60000);
  auto *voltage = this->add_sensor(1);
  auto *relay = this->add_sensor(117, MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);
  // An hour of silence, so this one is judged by a bound the test never reaches.
  auto *other_device = this->add_device(BATTERY_6);
  other_device->set_timeout(3600000);
  auto *other_voltage = this->add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT,
                                         MasterbusTab::MASTERBUS_TAB_MONITORING, other_device);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(117, 1.0f));
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_6), true, false, monitoring_answer(1, 26.0f));
  const uint32_t answered_at = App.get_loop_component_start_time();

  this->hub_->check_availability(answered_at + 59999);
  EXPECT_EQ(voltage->unavailable_count, 0);
  EXPECT_EQ(relay->unavailable_count, 0);

  this->hub_->check_availability(answered_at + 60000);
  EXPECT_EQ(voltage->unavailable_count, 1);
  EXPECT_EQ(relay->unavailable_count, 1);
  EXPECT_FALSE(this->battery_->is_online());

  // Reported once, not once a second for as long as the device stays quiet.
  this->hub_->check_availability(answered_at + 600000);
  EXPECT_EQ(voltage->unavailable_count, 1);
  EXPECT_EQ(relay->unavailable_count, 1);

  // One answer is enough to bring the device back, and its fields publish again as they arrive.
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.3f));
  EXPECT_TRUE(this->battery_->is_online());
  ASSERT_EQ(voltage->values.size(), 2u);

  // And none of it touched the device that never went quiet.
  EXPECT_EQ(other_voltage->unavailable_count, 0);
  EXPECT_TRUE(other_device->is_online());
}

// One sweep, three devices, three answers: each is judged against the silence it was given, not
// against the sweep's own clock.
TEST_F(MasterbusTest, TheSweepJudgesEachDeviceOnItsOwnTimeout) {
  this->battery_->set_timeout(60000);
  auto *patient = this->add_device(BATTERY_6);
  patient->set_timeout(3600000);
  auto *never_heard = this->add_device(0x111111);
  never_heard->set_timeout(60000);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_6), true, false, monitoring_answer(1, 26.0f));
  const uint32_t answered_at = App.get_loop_component_start_time();

  this->hub_->check_availability(answered_at + 60000);

  EXPECT_FALSE(this->battery_->is_online());
  EXPECT_TRUE(patient->is_online());
  // A device that has never answered was never online, so the sweep has nothing to take down.
  EXPECT_FALSE(never_heard->is_online());
}

TEST_F(MasterbusTest, DeviceTriggersFireOnTheEdgesOnly) {
  this->battery_->set_timeout(60000);
  int online = 0;
  int offline = 0;
  this->battery_->add_on_online_callback([&online]() { online++; });
  this->battery_->add_on_offline_callback([&offline]() { offline++; });

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  const uint32_t answered_at = App.get_loop_component_start_time();
  EXPECT_EQ(online, 1);

  // A second answer is not a second arrival.
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.3f));
  EXPECT_EQ(online, 1);

  this->hub_->check_availability(answered_at + 60000);
  EXPECT_EQ(offline, 1);
  this->hub_->check_availability(answered_at + 61000);
  EXPECT_EQ(offline, 1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.4f));
  EXPECT_EQ(online, 2);
  EXPECT_EQ(offline, 1);
}

TEST_F(MasterbusTest, FrameFromAnUndeclaredDeviceLeavesUsOffline) {
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, UNDECLARED_DEVICE), true, false,
                       monitoring_answer(1, 26.241f));

  EXPECT_FALSE(this->battery_->is_online());
}

TEST_F(MasterbusTest, AFieldWithItsOwnTimeoutGoesUnavailableOnItsOwn) {
  // A relay changes twice a day while the voltage beside it arrives every second, so the device's
  // timeout cannot speak for both. Only a field given a timeout of its own is judged on it.
  auto *relay = this->add_sensor(117, MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);
  relay->set_stale_timeout(60000);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(117, 1.0f));
  ASSERT_EQ(relay->values.size(), 1u);
  const uint32_t answered_at = App.get_loop_component_start_time();

  relay->check_stale(answered_at + 59999);
  EXPECT_EQ(relay->unavailable_count, 0);

  relay->check_stale(answered_at + 60000);
  EXPECT_EQ(relay->unavailable_count, 1);

  // Reported once, not once a second for as long as it stays quiet.
  relay->check_stale(answered_at + 600000);
  EXPECT_EQ(relay->unavailable_count, 1);

  // A value brings it back, so the next silence is reported again rather than swallowed.
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(117, 0.0f));
  relay->check_stale(App.get_loop_component_start_time() + 60000);
  EXPECT_EQ(relay->unavailable_count, 2);
}

TEST_F(MasterbusTest, AFieldWithoutItsOwnTimeoutFollowsItsDevice) {
  auto *voltage = this->add_sensor(1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  voltage->check_stale(App.get_loop_component_start_time() + 3600000);

  EXPECT_EQ(voltage->unavailable_count, 0);
}

TEST_F(MasterbusTest, AnEntityAsksForItsOwnField) {
  auto *voltage = this->add_sensor(1);
  this->add_sensor(2);

  voltage->update();

  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  const auto &frame = this->canbus_.sent[0];
  EXPECT_TRUE(frame.use_extended_id);
  EXPECT_EQ(frame.can_id >> MESSAGE_TYPE_SHIFT, MONITORING_REQUEST_TYPE);
  EXPECT_EQ(frame.can_id & DEVICE_ADDRESS_MASK, BATTERY_1);
  EXPECT_EQ(frame.can_data_length_code, MONITORING_REQUEST_LENGTH);
  EXPECT_EQ(encode_uint16(frame.data[1], frame.data[0]), 1);
}

TEST_F(MasterbusTest, AnAnswerMeantForSomeoneElseSuppressesOurOwnRequest) {
  // A MasterBus answer is broadcast, so a bridge or a display panel asking the same device for the
  // same field updates this entity too. Asking again straight after adds traffic and nothing else.
  auto *voltage = this->add_sensor(1);
  voltage->set_update_interval(10000);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  ASSERT_EQ(voltage->values.size(), 1u);
  this->canbus_.clear();

  voltage->update();

  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, OurOwnAnswerDoesNotSuppressTheNextRequest) {
  // The answer to a request is a full cadence old by the time the next one is due, so treating it
  // as a fresh reading from somebody else would halve the polling rate. Measured on a live bus:
  // a field nobody else wanted was asked for every 20 s under a 10 s update_interval.
  auto *voltage = this->add_sensor(1);
  voltage->set_update_interval(10000);

  voltage->update();
  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  ASSERT_EQ(voltage->values.size(), 1u);
  this->canbus_.clear();

  voltage->update();

  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  EXPECT_EQ(this->canbus_.sent[0].can_id, 0x186D56EA);
}

TEST_F(MasterbusTest, AnAnswerOlderThanTheCadenceDoesNotSuppressOurRequest) {
  // Same setup, except the cadence has already elapsed since that answer, so the value is no
  // longer fresh enough to stand in for one of our own.
  auto *voltage = this->add_sensor(1);
  voltage->set_update_interval(0);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  ASSERT_EQ(voltage->values.size(), 1u);
  this->canbus_.clear();

  voltage->update();

  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  EXPECT_EQ(this->canbus_.sent[0].can_id, 0x186D56EA);
}

TEST_F(MasterbusTest, WritingABooleanSendsTheValueAndTheFrameThatFollowsIt) {
  // Captured from the vendor library closing a battery relay: the write is a monitoring request
  // carrying a float, and a second frame to the next field number always follows it.
  auto *relay = this->add_sensor(117);

  ASSERT_TRUE(this->hub_->write_boolean(*relay, true));

  ASSERT_EQ(this->canbus_.sent.size(), 2u);
  const auto &write = this->canbus_.sent[0];
  EXPECT_EQ(write.can_id, 0x186D56EA);
  ASSERT_EQ(write.can_data_length_code, MONITORING_WRITE_LENGTH);
  EXPECT_EQ(encode_uint16(write.data[1], write.data[0]), 117);
  EXPECT_EQ(encode_uint32(write.data[5], write.data[4], write.data[3], write.data[2]), 0x3F800000);

  const auto &commit = this->canbus_.sent[1];
  EXPECT_EQ(commit.can_id, 0x186D56EA);
  ASSERT_EQ(commit.can_data_length_code, MONITORING_WRITE_LENGTH);
  EXPECT_EQ(encode_uint16(commit.data[1], commit.data[0]), 118);
  EXPECT_EQ(commit.data[2], 0x01);
  EXPECT_EQ(commit.data[3], 0x00);
  EXPECT_EQ(commit.data[4], 0x50);
  EXPECT_EQ(commit.data[5], 0x00);
}

// Reading the configuration tab works; writing it does not. The two are separate decisions, and
// this is the pair that keeps them separate.
TEST_F(MasterbusTest, WriteSkipsTabsWithNoKnownWriteFormat) {
  auto *setting =
      this->add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

  EXPECT_FALSE(this->hub_->write_boolean(*setting, true));
  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, PollSkipsTheAlarmTabBecauseItsValueMessageIsNotDecoded) {
  // The alarm tab is the one tab the vendor does not give a Data message alongside the others, so
  // its structure can be walked but its values cannot be asked for.
  auto *alarm = this->add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_ALARM);

  alarm->update();

  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, AConfigurationFieldIsPolledWithItsOwnMessageNumber) {
  auto *setting =
      this->add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

  setting->update();

  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  const auto &frame = this->canbus_.sent[0];
  EXPECT_EQ(frame.can_id >> MESSAGE_TYPE_SHIFT, 0x37);
  EXPECT_EQ(frame.can_id & DEVICE_ADDRESS_MASK, BATTERY_1);
  // The payload is monitoring's: the field number and nothing else.
  ASSERT_EQ(frame.can_data_length_code, MONITORING_REQUEST_LENGTH);
  EXPECT_EQ(encode_uint16(frame.data[1], frame.data[0]), 1);
}

TEST_F(MasterbusTest, AValueIsPublishedToTheTabItCameFrom) {
  // Two entities, same device, same field number, different tabs. Nothing but the message number
  // distinguishes their answers, so this is what a wrong lookup would break.
  auto *monitoring = this->add_sensor(1);
  auto *setting =
      this->add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  ASSERT_EQ(monitoring->values.size(), 1u);
  EXPECT_TRUE(setting->values.empty());

  this->hub_->on_frame(frame_id(0x17, BATTERY_1), true, false, monitoring_answer(1, 55.0f));
  ASSERT_EQ(setting->values.size(), 1u);
  EXPECT_FLOAT_EQ(setting->values[0].as_float, 55.0f);
  EXPECT_EQ(monitoring->values.size(), 1u) << "a configuration answer reached a monitoring entity";
}

TEST_F(MasterbusTest, AnAnswerOfTheWrongLengthOnADerivedTabIsDropped) {
  // The history and configuration payload layout is assumed from monitoring's, not measured. If
  // the assumption is wrong the answer will not be six bytes, and dropping it is what turns a
  // wrong guess into silence rather than into a plausible wrong reading.
  auto *setting =
      this->add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

  this->hub_->on_frame(frame_id(0x17, BATTERY_1), true, false, {0x01, 0x00, 0x00, 0x60});

  EXPECT_TRUE(setting->values.empty());
}

// Announcement payloads captured from the live bus, one per device. The first four bytes carry
// the sender's own address, which is what makes discovery possible without transmitting.
TEST_F(MasterbusTest, ScanDiscoversDevicesFromTheirAnnouncements) {
  // 0x046D56EA is the identifier BAT 1 announces itself under.
  ASSERT_EQ(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), 0x046D56EAu);
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_6), true, false,
                       {0x1B, 0xCB, 0x4E, 0x00, 0x95, 0x03, 0x00, 0x04});

  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[0].address, BATTERY_1);
  EXPECT_EQ(found[1].address, BATTERY_6);
}

TEST_F(MasterbusTest, ScanFindsDevicesNobodyDeclared) {
  // 0x535E30 is a display panel that was never configured. Discovery must still list it.
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, 0x535E30), true, false,
                       {0x14, 0x30, 0x5E, 0x03, 0x12, 0x01, 0x00, 0x00});

  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].address, 0x535E30u);
}

TEST_F(MasterbusTest, AnnouncementKeepsToTheAddressBitsTheIdentifierCarries) {
  // The first payload byte has room for six bits, but sixteen plus two plus five is the 23 bits
  // the identifier gives the address. Shifting a sixth bit in would put the device above the
  // range any configuration can declare, so it could never be matched again.
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x3B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});

  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_LE(found[0].address, MAX_DEVICE_ADDRESS);
  EXPECT_EQ(found[0].address, BATTERY_1);
}

TEST_F(MasterbusTest, RepeatedAnnouncementsCountRatherThanDuplicate) {
  for (int i = 0; i < 3; i++) {
    this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                         {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});
  }

  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].announcements, 3u);
}

TEST_F(MasterbusTest, ReportingTheScanTransmitsNothing) {
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();

  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, NodeRequestIsAnEmptyFrameToTheBroadcastAddress) {
  // The whole message is the identifier: the node request type against an address that belongs to
  // no device.
  // Captured from the vendor library, which repeats it so a device that missed one still answers.
  EXPECT_TRUE(this->hub_->request_nodes());

  ASSERT_EQ(this->canbus_.sent.size(), NODE_REQUEST_REPEATS);
  for (const auto &frame : this->canbus_.sent) {
    EXPECT_TRUE(frame.use_extended_id);
    EXPECT_EQ(frame.can_data_length_code, 0);
    // The literal identifier recorded from the vendor library. Asserting the whole value rather
    // than the parts is what catches a wrong split between type and address.
    EXPECT_EQ(frame.can_id, 0x05500001u);
  }
}

TEST_F(MasterbusTest, ScanCollectsWhatTheNodeRequestBringsBack) {
  this->hub_->request_nodes();
  // Every device answers within a few milliseconds; two of the eight recorded on the real bus.
  this->feed("ext 0x046D56EA [8] 1B:EA:56:01:51:00:00:02");
  this->feed("ext 0x04535E30 [8] 14:30:5E:03:12:01:00:00");

  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[0].address, BATTERY_1);
  EXPECT_EQ(found[1].address, 0x535E30u);
}

// The scan's second half: walking a device for its fields. Frames below are shaped the way the
// bus shapes them, with the values the vendor library reported for the same field.
TEST_F(MasterbusTest, ScanAsksForAGroupsFieldCount) {
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});
  this->canbus_.clear();

  this->hub_->report_scan();
  this->hub_->scan_step(this->scan_now_);

  ASSERT_FALSE(this->canbus_.sent.empty());
  const auto &frame = this->canbus_.sent.back();
  EXPECT_EQ(frame.can_id >> MESSAGE_TYPE_SHIFT, GROUP_REQUEST_TYPE);
  EXPECT_EQ(frame.can_id & DEVICE_ADDRESS_MASK, BATTERY_1);
  ASSERT_EQ(frame.can_data_length_code, 3);
  EXPECT_EQ(frame.data[0], static_cast<uint8_t>(MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_COUNT));
}

TEST_F(MasterbusTest, ScanReadsAFieldsMetadataAndNames) {
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();

  // Group 0 holds seven fields - the count arrives as a float, oddly enough.
  this->hub_->scan_step(this->scan_now_);
  this->hub_->on_frame(frame_id(GROUP_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  // String 0 means the group has no name, which is normal and does not stop the walk.
  this->hub_->scan_step(this->scan_now_);
  this->hub_->on_frame(frame_id(GROUP_INFORMATION_TYPE, BATTERY_1), true, false, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});
  // Index 0 is field 1.
  this->hub_->scan_step(this->scan_now_);
  this->hub_->on_frame(frame_id(GROUP_INFORMATION_TYPE, BATTERY_1), true, false, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  // Display type 1, float - so a sensor.
  this->hub_->scan_step(this->scan_now_);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  // Name string 97, unit string 10.
  this->hub_->scan_step(this->scan_now_);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});
  this->hub_->scan_step(this->scan_now_);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x2C, 0x01, 0x00, 0x00, 0x0A, 0x00});

  const auto &field = this->hub_->get_scanned_field();
  EXPECT_EQ(field.param, 1);
  EXPECT_EQ(field.display_type, MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_FLOAT);
}

TEST_F(MasterbusTest, ScanReassemblesAStringFromItsChunks) {
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();

  // Answer every question in turn so the walk reaches the string reads without waiting on a clock.
  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x50, 0x00});  // group name is string 80
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x50, 0x00, 0x00, 'B', 'a', 'n', 'k'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x50, 0x00, 0x01, 0x00});
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});  // name string 97
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x0A, 0x00});  // unit string 10
  this->answer(PROPERTY_INFORMATION_TYPE, {0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x16, 0x44});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x08, 0x01, 0x00, 0x00, 0x0A, 0xD7, 0x23, 0x3C});

  // "Battery" arrives as "Batt" then "ery" with its terminator.
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});

  EXPECT_STREQ(this->hub_->get_scanned_group_name(), "Bank");
  EXPECT_STREQ(this->hub_->get_scanned_field().name, "Battery");
}

TEST_F(MasterbusTest, ScanPlacesStringChunksByTheirOwnHeader) {
  // Chunks are placed where the answer says they belong, not where a counter guesses. A chunk of
  // some other string is not ours to take: reading it as the next piece of this one produced
  // names like "Batt%" on real equipment.
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();

  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});  // group has no name
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});  // name string 97
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x0A, 0x00});  // unit string 10
  this->answer(PROPERTY_INFORMATION_TYPE, {0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x16, 0x44});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x08, 0x01, 0x00, 0x00, 0x0A, 0xD7, 0x23, 0x3C});

  // A chunk of string 98 arrives in the middle; it belongs to nobody here.
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x62, 0x00, 0x00, 'Z', 'Z', 'Z', 'Z'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});

  EXPECT_STREQ(this->hub_->get_scanned_field().name, "Battery");
}

// The vendor's own message list, read out of its shared library. Six of the twelve entries are
// pinned by traffic; the rest follow from the same ordering. These assert the pinned ones, because
// if the ordering is ever edited it is those that must not move.
TEST_F(MasterbusTest, MessageNumbersMatchTheTrafficThatPinsThem) {
  EXPECT_EQ(masterbus_request_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_DATA), 0x30);
  EXPECT_EQ(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_DATA), 0x10);
  EXPECT_EQ(masterbus_request_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_PROPERTY), 0x31);
  EXPECT_EQ(masterbus_request_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_GROUP), 0x32);
  // Seen on an unrelated installation: a display panel walking the other tabs' group lists.
  EXPECT_EQ(masterbus_request_type(MasterbusMessage::MASTERBUS_MESSAGE_ALARM_GROUP), 0x34);
  EXPECT_EQ(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_ALARM_GROUP), 0x14);
  EXPECT_EQ(masterbus_request_type(MasterbusMessage::MASTERBUS_MESSAGE_HISTORY_GROUP), 0x36);
  EXPECT_EQ(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_HISTORY_GROUP), 0x16);
  EXPECT_EQ(masterbus_request_type(MasterbusMessage::MASTERBUS_MESSAGE_CONFIGURATION_GROUP), 0x39);
  EXPECT_EQ(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_CONFIGURATION_GROUP), 0x19);
}

TEST_F(MasterbusTest, TheAlarmTabHasNoDataMessageAndTheOthersDo) {
  // The vendor files AlarmData with the broadcast messages rather than with the tabs, so the alarm
  // tab can be walked for what it holds without its values being readable. Anything that reads a
  // value has to ask first.
  MasterbusMessage message;
  EXPECT_FALSE(tab_data_message(MasterbusTab::MASTERBUS_TAB_ALARM, message));
  EXPECT_TRUE(tab_data_message(MasterbusTab::MASTERBUS_TAB_MONITORING, message));
  EXPECT_EQ(masterbus_request_type(message), 0x30);
  EXPECT_TRUE(tab_data_message(MasterbusTab::MASTERBUS_TAB_CONFIGURATION, message));
  EXPECT_EQ(masterbus_request_type(message), 0x37);
  EXPECT_TRUE(tab_data_message(MasterbusTab::MASTERBUS_TAB_HISTORY, message));
  EXPECT_EQ(masterbus_request_type(message), 0x3A);
}

TEST_F(MasterbusTest, AStructuralRequestCarriesItsTabsOwnMessageNumber) {
  this->hub_->request_group(BATTERY_1, MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_COUNT, 0,
                            MasterbusTab::MASTERBUS_TAB_ALARM);
  this->hub_->request_property(BATTERY_1, MasterbusProperty::MASTERBUS_PROPERTY_DISPLAY_TYPE, 1,
                               MasterbusTab::MASTERBUS_TAB_ALARM);
  this->hub_->request_group(BATTERY_1, MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_COUNT, 0,
                            MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

  ASSERT_EQ(this->canbus_.sent.size(), 3u);
  EXPECT_EQ(this->canbus_.sent[0].can_id, frame_id(0x34, BATTERY_1));
  EXPECT_EQ(this->canbus_.sent[1].can_id, frame_id(0x33, BATTERY_1));
  EXPECT_EQ(this->canbus_.sent[2].can_id, frame_id(0x39, BATTERY_1));
  // The payload is the same on every tab; only the number on the identifier moves.
  EXPECT_EQ(this->canbus_.sent[0].can_data_length_code, 3);
  EXPECT_EQ(this->canbus_.sent[0].data[0],
            static_cast<uint8_t>(MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_COUNT));
}

TEST_F(MasterbusTest, TheScanIgnoresAnAnswerFromAnotherTab) {
  // Every tab answers with the same payload shape, so the message number is the only thing that
  // says which tab an answer belongs to. Taking an alarm answer for a monitoring one would put a
  // field from the wrong tab into the configuration the scan prints.
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();
  this->hub_->scan_step(this->scan_now_);
  const size_t asked = this->canbus_.sent.size();

  // The walk starts on monitoring and is waiting for 0x12. This is the alarm tab's answer.
  this->hub_->on_frame(frame_id(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_ALARM_GROUP), BATTERY_1),
                       true, false, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  this->hub_->scan_step(this->scan_now_);
  EXPECT_EQ(this->canbus_.sent.size(), asked) << "the scan moved on after an answer from another tab";

  // The same payload under monitoring's own number is taken, and the walk asks its next question.
  this->hub_->on_frame(
      frame_id(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_GROUP), BATTERY_1), true,
      false, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  this->hub_->scan_step(this->scan_now_);
  ASSERT_EQ(this->canbus_.sent.size(), asked + 1);
  EXPECT_EQ(this->canbus_.sent.back().data[0],
            static_cast<uint8_t>(MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_NAME_STRING));
}

// The hub-level catch for anything undecoded. It has to sit ahead of the device list, because the
// frames worth catching may come from equipment nobody declared - and because the message that
// carries an alarm is one of the things still missing, this is what will find it.
TEST_F(MasterbusTest, AnUndecodedMessageTypeReachesTheHubTrigger) {
  std::vector<std::tuple<std::vector<uint8_t>, uint8_t, uint32_t>> seen;
  this->hub_->add_on_unknown_frame_callback([&seen](const std::vector<uint8_t> &data, uint8_t type, uint32_t device) {
    seen.emplace_back(data, type, device);
  });

  // 0x04 is in no family this component can name. UNDECLARED_DEVICE is deliberate: a device-level
  // trigger could not have seen this at all.
  this->hub_->on_frame(frame_id(0x04, UNDECLARED_DEVICE), true, false, {0x1B, 0xEA, 0x56});

  ASSERT_EQ(seen.size(), 1u);
  EXPECT_EQ(std::get<1>(seen[0]), 0x04);
  EXPECT_EQ(std::get<2>(seen[0]), UNDECLARED_DEVICE);
  EXPECT_EQ(std::get<0>(seen[0]), (std::vector<uint8_t>{0x1B, 0xEA, 0x56}));
}

TEST_F(MasterbusTest, EveryMessageTheComponentCanNameIsLeftAlone) {
  int fired = 0;
  this->hub_->add_on_unknown_frame_callback([&fired](const std::vector<uint8_t> &, uint8_t, uint32_t) { fired++; });

  // One from each family: the twelve tab messages at all three bases, then node and string.
  for (uint8_t message = 0; message < MESSAGE_COUNT; message++) {
    for (uint8_t base : {MESSAGE_INFORMATION_BASE, MESSAGE_NOT_AVAILABLE_BASE, MESSAGE_REQUEST_BASE})
      this->hub_->on_frame(frame_id(base + message, BATTERY_1), true, false, {0x00});
  }
  for (uint8_t type : {DEVICE_ANNOUNCEMENT_TYPE, NODE_NOT_AVAILABLE_TYPE, NODE_REQUEST_TYPE, STRING_INFORMATION_TYPE,
                       STRING_NOT_AVAILABLE_TYPE, STRING_REQUEST_TYPE})
    this->hub_->on_frame(frame_id(type, BATTERY_1), true, false, {0x00});

  EXPECT_EQ(fired, 0);

  // And the gap either side of the tab block is still reported, so the bounds are not off by one.
  this->hub_->on_frame(frame_id(MESSAGE_INFORMATION_BASE - 1, BATTERY_1), true, false, {0x00});
  this->hub_->on_frame(frame_id(MESSAGE_REQUEST_BASE + MESSAGE_COUNT, BATTERY_1), true, false, {0x00});
  EXPECT_EQ(fired, 2);
}

// The scan's lines are what the converter on the documentation page reads, so they are pinned
// exactly: a device, a named group, and one field the device described completely.
TEST_F(MasterbusTest, AScanReportsWhatTheDeviceAnsweredAsKeyAndValue) {
  scan_lines().clear();
  this->feed("ext 0x046D56EA [8] 1B:EA:56:01:51:00:00:02");
  this->hub_->report_scan();

  // One field in group 0, the count arriving as a float.
  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F});
  // The group is called "Bank", through string 80.
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x50, 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x50, 0x00, 0x00, 'B', 'a', 'n', 'k'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x50, 0x00, 0x01, 0x00});
  // Index 0 of the group is field 1, a float, named by string 97 and measured in string 10.
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x0A, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x70, 0x42});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x08, 0x01, 0x00, 0x00, 0xCD, 0xCC, 0xCC, 0x3D});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x0A, 0x00, 0x00, 'V', 0x00});

  ASSERT_EQ(scan_lines().lines().size(), 2u);
  EXPECT_EQ(scan_lines().lines()[0], "group device=0x6D56EA tab=0 group=0 name=\"Bank\"");
  EXPECT_EQ(scan_lines().lines()[1],
            "field device=0x6D56EA tab=0 group=0 param=1 display=1 name=\"Battery\" unit=\"V\" min=0 max=60 "
            "step=0.1");
}

// What a device did not answer for is absent, rather than reported as a value it never sent.
TEST_F(MasterbusTest, AFieldTheDeviceBarelyDescribesReportsOnlyWhatItAnswered) {
  scan_lines().clear();
  this->feed("ext 0x046D56EA [8] 1B:EA:56:01:51:00:00:02");
  this->hub_->report_scan();

  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F});
  // String 0: the group has no name at all.
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x75, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x75, 0x00, 0x00, 0x05, 0x00});
  // No name string, no unit string, and nothing answered for minimum, maximum or step.
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x75, 0x00, 0x00, 0x00, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x75, 0x00, 0x00, 0x00, 0x00});
  this->unanswered();  // minimum
  this->unanswered();  // maximum
  this->unanswered();  // step

  ASSERT_EQ(scan_lines().lines().size(), 2u);
  EXPECT_EQ(scan_lines().lines()[0], "group device=0x6D56EA tab=0 group=0");
  EXPECT_EQ(scan_lines().lines()[1], "field device=0x6D56EA tab=0 group=0 param=117 display=5");
}

// Asking the string table for entry zero would cost an answer timeout on every field that
// carries no unit, and most of them do not.
TEST_F(MasterbusTest, AFieldWithNoUnitIsNotAskedForTheUnitString) {
  this->feed("ext 0x046D56EA [8] 1B:EA:56:01:51:00:00:02");
  this->hub_->report_scan();

  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F});
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x75, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x75, 0x00, 0x00, 0x05, 0x00});
  // Neither a name string nor a unit string.
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x75, 0x00, 0x00, 0x00, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x75, 0x00, 0x00, 0x00, 0x00});

  this->canbus_.clear();
  this->unanswered();  // minimum
  this->unanswered();  // maximum
  this->unanswered();  // step
  this->hub_->scan_step(this->scan_now_);

  for (const auto &frame : this->canbus_.sent)
    EXPECT_NE(frame.can_id >> MESSAGE_TYPE_SHIFT, STRING_REQUEST_TYPE)
        << "the walk asked the string table for a field that carries no strings";
}

// The flag saying a group carries a name belongs to the group being walked, not to the last one
// that had one.
TEST_F(MasterbusTest, AGroupThatDoesNotAnswerForItsNameReportsNoNameAtAll) {
  scan_lines().clear();
  this->feed("ext 0x046D56EA [8] 1B:EA:56:01:51:00:00:02");
  this->hub_->report_scan();

  // Group 0 holds one field and is called "Bank", through string 80.
  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F});
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x50, 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x50, 0x00, 0x00, 'B', 'a', 'n', 'k'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x50, 0x00, 0x01, 0x00});
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x05, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x00, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x00, 0x00});
  this->unanswered();  // minimum
  this->unanswered();  // maximum
  this->unanswered();  // step
  this->unanswered();  // the group holds no second field

  // Group 1 holds one field too, but says nothing when asked what it is called.
  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F});
  this->unanswered();  // the name question
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x01, 0x00, 0x00, 0x02, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x02, 0x00, 0x00, 0x05, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x02, 0x00, 0x00, 0x00, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x02, 0x00, 0x00, 0x00, 0x00});
  this->unanswered();  // minimum
  this->unanswered();  // maximum
  this->unanswered();  // step

  ASSERT_EQ(scan_lines().lines().size(), 4u);
  EXPECT_EQ(scan_lines().lines()[0], "group device=0x6D56EA tab=0 group=0 name=\"Bank\"");
  EXPECT_EQ(scan_lines().lines()[2], "group device=0x6D56EA tab=0 group=1");
}

}  // namespace esphome::masterbus::testing
