#include "common.h"

namespace esphome::masterbus::testing {

// Addresses and payloads below are taken verbatim from a capture of a live installation.
// 0x6D56EA and 0x6C4ECB are two of the battery blocks the vendor library reports by the same
// number, which is what pins the low 24 bits of the identifier as the device address.
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

  RecordingEntity *add_sensor(uint16_t param, MasterbusValueType type = MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT,
                              MasterbusTab tab = MasterbusTab::MASTERBUS_TAB_MONITORING) {
    auto entity = std::make_unique<RecordingEntity>(this->battery_.get(), param, tab, type);
    auto *raw = entity.get();
    this->hub_->register_entity(raw);
    this->entities_.push_back(std::move(entity));
    return raw;
  }

  /// Let the walk send its next question, then hand it the answer.
  void answer(uint8_t type, const std::vector<uint8_t> &data) {
    this->hub_->scan_step();
    this->hub_->on_frame(frame_id(type, BATTERY_1), true, false, data);
  }

  RecordingCanbus canbus_;
  std::unique_ptr<MasterbusHub> hub_;
  std::unique_ptr<MasterbusDevice> battery_;
  std::vector<std::unique_ptr<RecordingEntity>> entities_;
};

TEST_F(MasterbusTest, MonitoringAnswerUpdatesTheMatchingSensor) {
  auto *voltage = add_sensor(1);
  auto *current = add_sensor(2);

  // (…) can_mb 086D56EA#0100 92EDD141 -> 26.241 V on field 1
  // 0x086D56EA is the identifier as it appears on the wire for a monitoring answer from BAT 1.
  ASSERT_EQ(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), 0x086D56EAu);
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));

  ASSERT_EQ(voltage->values.size(), 1u);
  EXPECT_FLOAT_EQ(voltage->values[0].as_float, 26.241f);
  EXPECT_TRUE(current->values.empty());
}

TEST_F(MasterbusTest, AnswerForAnotherDeviceIsIgnored) {
  auto *voltage = add_sensor(1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_6), true, false, monitoring_answer(1, 26.241f));

  EXPECT_TRUE(voltage->values.empty());
}

TEST_F(MasterbusTest, RequestFrameIsNotMistakenForAnAnswer) {
  auto *voltage = add_sensor(1);

  // A request carries the field number and nothing else. Reading it as an answer would publish
  // whatever followed in memory.
  this->hub_->on_frame(frame_id(MONITORING_REQUEST_TYPE, BATTERY_1), true, false, {0x01, 0x00});

  EXPECT_TRUE(voltage->values.empty());
}

TEST_F(MasterbusTest, ShortPayloadIsDropped) {
  auto *voltage = add_sensor(1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, {0x01, 0x00, 0x92, 0xED});

  EXPECT_TRUE(voltage->values.empty());
}

TEST_F(MasterbusTest, StandardIdentifierIsIgnored) {
  auto *voltage = add_sensor(1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), false, false, monitoring_answer(1, 26.241f));

  EXPECT_TRUE(voltage->values.empty());
}

TEST_F(MasterbusTest, BooleanFieldReadsAsBoolean) {
  // Field 117 "Close relay" arrives as a float 0.0 or 1.0; declaring it boolean is what makes it
  // a state rather than a number.
  auto *relay = add_sensor(117, MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(117, 1.0f));

  ASSERT_EQ(relay->values.size(), 1u);
  EXPECT_EQ(relay->values[0].type, MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);
  EXPECT_TRUE(relay->values[0].as_boolean);
}

TEST_F(MasterbusTest, NotANumberReportsUnavailableRatherThanZero) {
  auto *voltage = add_sensor(1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, NAN));

  EXPECT_TRUE(voltage->values.empty());
  EXPECT_EQ(voltage->unavailable_count, 1);
}

// A text field answers with the number of an entry in the device's string table rather than with
// text, so publishing one takes a second exchange. Frames below are shaped the way the bus shapes
// them; string 97 is the "Battery" the scan tests read through the same table.
TEST_F(MasterbusTest, TextFieldIsReadFromTheStringTable) {
  auto *label = add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);

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
  auto *label = add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);

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
  auto *label = add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 0.0f));

  EXPECT_TRUE(label->values.empty());
  EXPECT_EQ(label->unavailable_count, 1);
  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, StringTheDeviceDoesNotHaveLeavesTheFieldUnavailable) {
  auto *label = add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false, {0x30, 0x61, 0x00, 0x00});

  EXPECT_TRUE(label->values.empty());
  EXPECT_EQ(label->unavailable_count, 1);
}

TEST_F(MasterbusTest, ATextAnswerCountsAsAnAnswerBeforeItsTextArrives) {
  // The monitoring answer came from somebody else's request, so there is nothing to gain by
  // asking again - even though the text it points at has not been read yet.
  auto *label = add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);
  label->set_update_interval(10000);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  this->canbus_.clear();

  label->update();

  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, TimeFieldReadsAsSecondsSinceMidnight) {
  auto *clock = add_sensor(94, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(94, 45296.0f));

  ASSERT_EQ(clock->values.size(), 1u);
  EXPECT_EQ(clock->values[0].type, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);
  EXPECT_STREQ(clock->values[0].as_text, "12:34:56");
}

TEST_F(MasterbusTest, DateFieldUnpacksTheCalendarTheDevicePacked) {
  // 841202 is what a Mastervolt DC shunt reported on 2022-01-18, taken from a capture of an
  // unrelated installation. It is the whole of the evidence that months hold 32 days here.
  auto *today = add_sensor(95, MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(95, 841202.0f));

  ASSERT_EQ(today->values.size(), 1u);
  EXPECT_EQ(today->values[0].type, MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE);
  EXPECT_STREQ(today->values[0].as_text, "2022-01-18");
}

TEST_F(MasterbusTest, TimeKeepsItsLeadingZeroes) {
  auto *clock = add_sensor(94, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  // A minute and a second past midnight.
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(94, 61.0f));

  ASSERT_EQ(clock->values.size(), 1u);
  EXPECT_STREQ(clock->values[0].as_text, "00:01:01");
}

TEST_F(MasterbusTest, DateKeepsItsLeadingZeroes) {
  auto *today = add_sensor(95, MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(95, 841185.0f));

  ASSERT_EQ(today->values.size(), 1u);
  EXPECT_STREQ(today->values[0].as_text, "2022-01-01");
}

TEST_F(MasterbusTest, ATimeSpanIsNotFoldedIntoADay) {
  // The same display type serves a clock and a countdown. A DC shunt reporting hours of charge
  // left answered 579120 on the bus this was captured from; folding that into a day would report
  // a plausible and wrong time of day.
  auto *remaining = add_sensor(4, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(4, 579120.0f));

  ASSERT_EQ(remaining->values.size(), 1u);
  EXPECT_STREQ(remaining->values[0].as_text, "160:52:00");
}

TEST_F(MasterbusTest, ANumberThatCannotBeATimeIsUnavailableRatherThanTruncated) {
  // Casting a float outside the destination range is undefined, so a field declared as a time
  // that answers with something else is refused rather than truncated into a plausible one.
  auto *clock = add_sensor(94, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(94, -1.0f));
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false,
                       monitoring_answer(94, 235929600.0f));

  EXPECT_TRUE(clock->values.empty());
  EXPECT_EQ(clock->unavailable_count, 2);
}

// A number that publishes as text is rendered somewhere, and a string arrives in chunks across
// several frames. Rendering one where the other is being assembled loses the string.
TEST_F(MasterbusTest, ATimeAnswerDoesNotOverwriteAStringBeingRead) {
  auto *label = add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);
  auto *clock = add_sensor(94, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

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
  auto *label = add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);
  add_sensor(94, MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(94, 45296.0f));
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false, {0x30, 0x61, 0x00, 0x01, 0x00});

  ASSERT_EQ(label->values.size(), 1u);
  EXPECT_STREQ(label->values[0].as_text, "Batt");
}

TEST_F(MasterbusTest, AnyFrameFromTheDeviceMarksItOnline) {
  EXPECT_FALSE(this->battery_->is_online());

  // Type 0x04 is not decoded, but it still proves the device is answering.
  this->hub_->on_frame(frame_id(0x04, BATTERY_1), true, false, {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});

  EXPECT_TRUE(this->battery_->is_online());
}

TEST_F(MasterbusTest, FrameFromAnUndeclaredDeviceLeavesUsOffline) {
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, UNDECLARED_DEVICE), true, false,
                       monitoring_answer(1, 26.241f));

  EXPECT_FALSE(this->battery_->is_online());
}

TEST_F(MasterbusTest, AFieldWithItsOwnTimeoutGoesUnavailableOnItsOwn) {
  // A relay changes twice a day while the voltage beside it arrives every second, so the device's
  // timeout cannot speak for both. Only a field given a timeout of its own is judged on it.
  auto *relay = add_sensor(117, MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);
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
  auto *voltage = add_sensor(1);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(1, 26.241f));
  voltage->check_stale(App.get_loop_component_start_time() + 3600000);

  EXPECT_EQ(voltage->unavailable_count, 0);
}

TEST_F(MasterbusTest, AnEntityAsksForItsOwnField) {
  auto *voltage = add_sensor(1);
  add_sensor(2);

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
  auto *voltage = add_sensor(1);
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
  auto *voltage = add_sensor(1);
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
  auto *voltage = add_sensor(1);
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
  auto *relay = add_sensor(117);

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
      add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

  EXPECT_FALSE(this->hub_->write_boolean(*setting, true));
  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, PollSkipsTheAlarmTabBecauseItsValueMessageIsNotDecoded) {
  // The alarm tab is the one tab the vendor does not give a Data message alongside the others, so
  // its structure can be walked but its values cannot be asked for.
  auto *alarm = add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_ALARM);

  alarm->update();

  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, AConfigurationFieldIsPolledWithItsOwnMessageNumber) {
  auto *setting =
      add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

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
  auto *monitoring = add_sensor(1);
  auto *setting =
      add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

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
      add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

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
  // Every device answers within a few milliseconds; two of the eight seen on the real bus.
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0xEA, 0x56, 0x01, 0x51, 0x00, 0x00, 0x02});
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, 0x535E30), true, false,
                       {0x14, 0x30, 0x5E, 0x03, 0x12, 0x01, 0x00, 0x00});

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
  this->hub_->scan_step();

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
  this->hub_->scan_step();
  this->hub_->on_frame(frame_id(GROUP_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  // String 0 means the group has no name, which is normal and does not stop the walk.
  this->hub_->scan_step();
  this->hub_->on_frame(frame_id(GROUP_INFORMATION_TYPE, BATTERY_1), true, false, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});
  // Index 0 is field 1.
  this->hub_->scan_step();
  this->hub_->on_frame(frame_id(GROUP_INFORMATION_TYPE, BATTERY_1), true, false, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  // Display type 1, float - so a sensor.
  this->hub_->scan_step();
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  // Name string 97, unit string 10.
  this->hub_->scan_step();
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});
  this->hub_->scan_step();
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
  answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x50, 0x00});  // group name is string 80
  answer(STRING_INFORMATION_TYPE, {0x30, 0x50, 0x00, 0x00, 'B', 'a', 'n', 'k'});
  answer(STRING_INFORMATION_TYPE, {0x30, 0x50, 0x00, 0x01, 0x00});
  answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});  // name string 97
  answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x0A, 0x00});  // unit string 10
  answer(PROPERTY_INFORMATION_TYPE, {0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  answer(PROPERTY_INFORMATION_TYPE, {0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x16, 0x44});
  answer(PROPERTY_INFORMATION_TYPE, {0x08, 0x01, 0x00, 0x00, 0x0A, 0xD7, 0x23, 0x3C});

  // "Battery" arrives as "Batt" then "ery" with its terminator.
  answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});

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

  answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});  // group has no name
  answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});  // name string 97
  answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x0A, 0x00});  // unit string 10
  answer(PROPERTY_INFORMATION_TYPE, {0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  answer(PROPERTY_INFORMATION_TYPE, {0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x16, 0x44});
  answer(PROPERTY_INFORMATION_TYPE, {0x08, 0x01, 0x00, 0x00, 0x0A, 0xD7, 0x23, 0x3C});

  // A chunk of string 98 arrives in the middle; it belongs to nobody here.
  answer(STRING_INFORMATION_TYPE, {0x30, 0x62, 0x00, 0x00, 'Z', 'Z', 'Z', 'Z'});
  answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});

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
  this->hub_->scan_step();
  const size_t asked = this->canbus_.sent.size();

  // The walk starts on monitoring and is waiting for 0x12. This is the alarm tab's answer.
  this->hub_->on_frame(frame_id(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_ALARM_GROUP), BATTERY_1),
                       true, false, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  this->hub_->scan_step();
  EXPECT_EQ(this->canbus_.sent.size(), asked) << "the scan moved on after an answer from another tab";

  // The same payload under monitoring's own number is taken, and the walk asks its next question.
  this->hub_->on_frame(
      frame_id(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_GROUP), BATTERY_1), true,
      false, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  this->hub_->scan_step();
  ASSERT_EQ(this->canbus_.sent.size(), asked + 1);
  EXPECT_EQ(this->canbus_.sent.back().data[0],
            static_cast<uint8_t>(MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_NAME_STRING));
}

}  // namespace esphome::masterbus::testing
