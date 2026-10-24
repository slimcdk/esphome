#include "common.h"

namespace esphome::masterbus::testing {

// Two kinds of data appear below. Frames written as a line of log output were recorded off a live
// installation and are quoted as they were logged, with the capture line beside them; every other
// payload is built to the shape the protocol notes describe, to exercise one decision at a time.
//
// The device addresses are invented, wherever they appear: in a constant, in an identifier, and
// packed into an announcement or a device-list entry. The low 18 bits of an address are worked out
// from one unit's serial and article number, so a recorded address names one physical unit. The
// invented ones keep the device class in the top five bits - 27 a battery monitor, 20 a display
// panel, 14 an interface, 10 a charger - and the rest of each recorded frame as it was.
static constexpr uint32_t BATTERY_1 = 0x6C1001;
static constexpr uint32_t BATTERY_6 = 0x6C1002;
static constexpr uint32_t UNDECLARED_DEVICE = 0x281001;

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

  RecordingInfo *add_info(MasterbusDeviceInfo info) {
    auto entity = std::make_unique<RecordingInfo>(this->battery_.get(), info);
    auto *raw = entity.get();
    this->battery_->register_info_entity(raw);
    this->infos_.push_back(std::move(entity));
    return raw;
  }

  /// BAT 1's announcement, recorded off the bus, with byte 6 and the status version as given.
  void announce(uint8_t flags = 0x00, uint8_t status_version = 0x02) {
    this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                         {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, flags, status_version});
  }

  /// Let the information reader send its next question, then hand it the answer.
  void info_answer(uint8_t type, const std::vector<uint8_t> &data) {
    this->hub_->info_step(this->info_now_);
    this->hub_->on_frame(frame_id(type, BATTERY_1), true, false, data);
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

  /// Refuse one of the questions a device answers about itself, the way equipment that does not
  /// carry it does.
  void refuse_general_count(uint8_t question) {
    this->hub_->scan_step(this->scan_now_);
    this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false,
                         {GENERAL_COUNT_SELECTOR, question});
  }

  /// Answer how many groups a tab holds.
  void answer_group_count(uint16_t groups, MasterbusTab tab = MasterbusTab::MASTERBUS_TAB_MONITORING) {
    this->answer(STRING_INFORMATION_TYPE, {GENERAL_COUNT_SELECTOR, group_count_question(tab),
                                           static_cast<uint8_t>(groups & 0xFF), static_cast<uint8_t>(groups >> 8)});
  }

  /// Leave the walk to find the end of a group list by asking past it, which is what a device
  /// that does not carry the count forces.
  void refuse_group_count(MasterbusTab tab = MasterbusTab::MASTERBUS_TAB_MONITORING) {
    this->refuse_general_count(group_count_question(tab));
  }

  /// Take the walk past the questions it opens each device with: its eventables, answered, then
  /// everything else it asks about the device, refused the way equipment that does not carry it does.
  void answer_eventables(uint16_t count) {
    this->answer(STRING_INFORMATION_TYPE, {GENERAL_COUNT_SELECTOR, GENERAL_COUNT_EVENTABLES,
                                           static_cast<uint8_t>(count & 0xFF), static_cast<uint8_t>(count >> 8)});
    this->refuse_device_strings();
    this->refuse_software_version();
    this->refuse_general_counts();
  }

  /// Refuse the next question of the 0x0E family, the way a device does: its two byte header,
  /// sent back under 0x0D.
  void refuse_next() {
    this->hub_->scan_step(this->scan_now_);
    const auto &asked = this->canbus_.sent.back();
    this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false, {asked.data[0], asked.data[1]});
  }

  /// Refuse the device's four strings - article number, serial number, product name and revision
  /// code - in the order the walk asks for them.
  void refuse_device_strings() {
    for (uint8_t question = GENERAL_STRING_ARTICLE_NUMBER; question <= GENERAL_STRING_REVISION_CODE; question++) {
      this->hub_->scan_step(this->scan_now_);
      this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false,
                           {GENERAL_STRING_SELECTOR, question});
    }
  }

  /// Refuse the first processor's software version, which ends the list of processors at none.
  void refuse_software_version() {
    this->hub_->scan_step(this->scan_now_);
    this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false, {SOFTWARE_VERSION_SELECTOR, 0});
  }

  /// How many counts a device is asked once, after the line that names it: every one the vendor
  /// library accepts, but for the ones the walk asks on its own account.
  static unsigned general_count_round() {
    unsigned count = 0;
    for (unsigned question = 1; question <= GENERAL_COUNT_LAST_QUESTION; question++) {
      if (is_general_count_question(question) && question != GENERAL_COUNT_EVENTABLES &&
          question != GENERAL_COUNT_MONITORING_GROUPS && question != GENERAL_COUNT_HISTORY_GROUPS &&
          question != GENERAL_COUNT_CONFIGURATION_GROUPS && question != GENERAL_COUNT_ALARM_GROUPS)
        count++;
    }
    return count;
  }

  void refuse_general_counts() {
    for (unsigned i = 0; i < general_count_round(); i++)
      this->refuse_next();
  }

  /// Let the walk ask the three questions that follow a field's step - its factory value and the
  /// two flags - and hear nothing back, which is how a field that carries none of them answers.
  void unanswered_factory_and_flags() {
    this->unanswered();
    this->unanswered();
    this->unanswered();
    // Then the long name, which the walk stops asking a device for after three fields without one.
    if (this->long_names_unanswered_ < 3) {
      this->unanswered();
      this->long_names_unanswered_++;
    }
  }

  /// Let the walk send its next question and hear nothing back, which is how it learns that a
  /// list has ended or that a field does not carry a property.
  void unanswered() {
    this->hub_->scan_step(this->scan_now_);
    this->scan_now_ += SCAN_ANSWER_TIMEOUT_MS;
    this->hub_->scan_step(this->scan_now_);
  }

  /// Walk to a lone field 12 in group 0 of the configuration tab's first group - display type and
  /// maximum as given, nothing else answered - and leave the walk at the field's first list entry.
  void walk_to_list_field(MasterbusDisplayType display, float maximum) {
    this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
    this->hub_->report_scan();
    this->answer_eventables(2);
    this->answer_group_count(1);
    uint32_t bits;
    const float one = 1.0f;
    memcpy(&bits, &one, sizeof(bits));
    this->answer(GROUP_INFORMATION_TYPE,
                 {0x07, 0x00, 0x00, 0x00, static_cast<uint8_t>(bits), static_cast<uint8_t>(bits >> 8),
                  static_cast<uint8_t>(bits >> 16), static_cast<uint8_t>(bits >> 24)});
    this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});  // no group name
    this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 12, 0x00});
    this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 12, 0x00, 0x00, static_cast<uint8_t>(display), 0x00});
    this->unanswered();  // no name
    this->unanswered();  // no unit
    this->unanswered();  // no minimum
    memcpy(&bits, &maximum, sizeof(bits));
    this->answer(PROPERTY_INFORMATION_TYPE,
                 {0x07, 12, 0x00, 0x00, static_cast<uint8_t>(bits), static_cast<uint8_t>(bits >> 8),
                  static_cast<uint8_t>(bits >> 16), static_cast<uint8_t>(bits >> 24)});
    this->unanswered();  // no step
    this->unanswered_factory_and_flags();
  }

  /// The walk's own clock, moved on only by a question nobody answers.
  uint32_t scan_now_{0};
  uint8_t long_names_unanswered_{0};
  RecordingCanbus canbus_;
  std::unique_ptr<MasterbusHub> hub_;
  std::unique_ptr<MasterbusDevice> battery_;
  std::vector<std::unique_ptr<MasterbusDevice>> devices_;
  std::vector<std::unique_ptr<RecordingEntity>> entities_;
  std::vector<std::unique_ptr<RecordingInfo>> infos_;
  uint32_t info_now_{0};
};

TEST_F(MasterbusTest, MonitoringAnswerUpdatesTheMatchingSensor) {
  auto *voltage = this->add_sensor(1);
  auto *current = this->add_sensor(2);

  // Recorded: can_mb 086C1001#0100 92EDD141 -> 26.241 V on field 1. 0x086C1001 is the identifier
  // as it appears on the wire for a monitoring answer from BAT 1.
  ASSERT_EQ(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), 0x086C1001u);
  this->feed("ext 0x086C1001 [6] 01:00:92:ED:D1:41");

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

// A device list and an event field answer the way a list option does: with the index of an entry
// in a list the device holds. An index below zero is no entry at all, and must not wrap round to
// the last one.
TEST_F(MasterbusTest, ADeviceListAndAnEventFieldAnswerWithTheIndexOfAnEntry) {
  auto *devices = this->add_sensor(9, MasterbusValueType::MASTERBUS_VALUE_TYPE_DEVICE_ID);
  auto *event = this->add_sensor(10, MasterbusValueType::MASTERBUS_VALUE_TYPE_EVENTABLE);
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(9, 2.0f));
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(10, -1.0f));

  ASSERT_EQ(devices->values.size(), 1u);
  EXPECT_EQ(devices->values[0].type, MasterbusValueType::MASTERBUS_VALUE_TYPE_DEVICE_ID);
  EXPECT_EQ(devices->values[0].as_raw, 2u);
  ASSERT_EQ(event->values.size(), 1u);
  EXPECT_EQ(event->values[0].as_raw, UINT32_MAX);
}

// An index too large for any list, or infinite, is no entry either. Casting it would be undefined
// and in practice wraps round to some entry that exists.
TEST_F(MasterbusTest, AnIndexBeyondAnyListIsNoEntry) {
  auto *option = this->add_sensor(30, MasterbusValueType::MASTERBUS_VALUE_TYPE_LIST_OPTION);
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(30, INFINITY));
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(30, 1e10f));

  ASSERT_EQ(option->values.size(), 2u);
  EXPECT_EQ(option->values[0].as_raw, UINT32_MAX);
  EXPECT_EQ(option->values[1].as_raw, UINT32_MAX);
}

// A string id is two bytes. A text field answering anything outside that names no entry, and asking
// the table for whatever the cast made of it would publish some other string.
TEST_F(MasterbusTest, AStringIdNoStringTableCanHoldIsUnavailableAndAsksNothing) {
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);
  label->set_update_interval(10000);
  for (const float value : {-1.0f, 70000.0f, INFINITY})
    this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, value));

  EXPECT_TRUE(label->values.empty());
  EXPECT_EQ(label->unavailable_count, 3);
  EXPECT_TRUE(this->canbus_.sent.empty());
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
  label->set_update_interval(10000);

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
  label->set_update_interval(10000);

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

// A display panel reading the same string sees its answers broadcast like ours. Its answer to the
// last, short chunk arriving before ours to the first does not end our read with nothing in it.
TEST_F(MasterbusTest, AnotherNodesLaterChunkDoesNotEndATextRead) {
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);
  label->set_update_interval(10000);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});

  EXPECT_EQ(label->unavailable_count, 0);
  ASSERT_EQ(label->values.size(), 1u);
  EXPECT_STREQ(label->values[0].as_text, "Battery");
}

// A table entry without a terminator would otherwise be asked for one chunk after another for as
// long as the device answers. Once the buffer is full the text is published cut short, once.
TEST_F(MasterbusTest, AStringThatNeverEndsIsPublishedCutShortOnce) {
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);
  label->set_update_interval(10000);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  for (unsigned chunk = 0; chunk < 300; chunk++) {
    this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                         {0x30, 0x61, 0x00, static_cast<uint8_t>(chunk), 'x', 'y', 'z', 'w'});
  }

  // Eight chunks fill thirty-one characters and the terminator.
  EXPECT_EQ(this->canbus_.sent.size(), 8u);
  EXPECT_EQ(label->unavailable_count, 0);
  ASSERT_EQ(label->values.size(), 1u);
  EXPECT_STREQ(label->values[0].as_text, "xyzwxyzwxyzwxyzwxyzwxyzwxyzwxyz");
}

// An entity given no update_interval never transmits, and a text field is no exception: reading
// its string is a request like any other. Another node's answer for it is seen and left at that.
TEST_F(MasterbusTest, ATextFieldThatNeverPollsDoesNotReadItsString) {
  auto *label = this->add_sensor(24, MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));

  EXPECT_TRUE(this->canbus_.sent.empty());
  EXPECT_TRUE(label->values.empty());
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
  label->set_update_interval(10000);

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false, monitoring_answer(24, 97.0f));
  this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false, {0x30, 0x61, 0x00, 0x00});

  EXPECT_TRUE(label->values.empty());
  EXPECT_EQ(label->unavailable_count, 1);
}

// A bus that is off, or whose transmit queue is full, refuses every frame. The hub says so with its
// warning for as long as that lasts, rather than leaving it to devices timing out one by one.
TEST_F(MasterbusTest, TheHubWarnsWhileTheBusRefusesFrames) {
  auto *voltage = this->add_sensor(1);
  voltage->set_update_interval(5000);
  this->canbus_.error = canbus::ERROR_FAILTX;
  voltage->update();
  EXPECT_TRUE(this->hub_->status_has_warning());

  this->canbus_.error = canbus::ERROR_OK;
  voltage->update();
  EXPECT_FALSE(this->hub_->status_has_warning());
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
  label->set_update_interval(10000);
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
  label->set_update_interval(10000);
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
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});

  EXPECT_TRUE(this->battery_->is_online());
}

// A request carries the address of the device it is aimed at, never its sender's. A display panel
// polling a device that died would otherwise report it alive for as long as the panel kept asking.
TEST_F(MasterbusTest, AFrameAddressedToTheDeviceDoesNotProveItIsThere) {
  this->hub_->on_frame(frame_id(MONITORING_REQUEST_TYPE, BATTERY_1), true, false, {0x01, 0x00});
  EXPECT_FALSE(this->battery_->is_online());

  // Nor does a type that is no message at all: it may be somebody else's question.
  this->hub_->on_frame(frame_id(0x3A, BATTERY_1), true, false, {0x1B, 0x01, 0x10});
  EXPECT_FALSE(this->battery_->is_online());
}

// Which half of each family a frame belongs to is the whole of the rule above: answers and
// refusals come back from the device, requests go out to it.
TEST_F(MasterbusTest, OnlyTheHalfOfEachFamilyTheDeviceSendsCounts) {
  for (MasterbusTab tab : MASTERBUS_TABS) {
    for (MasterbusTabMessage message : MASTERBUS_TAB_MESSAGES) {
      const MasterbusMessageNumbers numbers = tab_message(tab, message);
      EXPECT_TRUE(device_sent_message(numbers.information)) << "answer " << +numbers.information;
      EXPECT_TRUE(device_sent_message(numbers.not_available)) << "refusal " << +numbers.not_available;
      EXPECT_FALSE(device_sent_message(numbers.request)) << "request " << +numbers.request;
    }
  }
  for (uint8_t type : {DEVICE_ANNOUNCEMENT_TYPE, STRING_INFORMATION_TYPE, STRING_NOT_AVAILABLE_TYPE,
                       BOOTLOADER_MESSAGE.information, BOOTLOADER_MESSAGE.not_available})
    EXPECT_TRUE(device_sent_message(type)) << "type " << +type;
  for (uint8_t type : {NODE_REQUEST_TYPE, STRING_REQUEST_TYPE, BOOTLOADER_MESSAGE.request})
    EXPECT_FALSE(device_sent_message(type)) << "type " << +type;

  // Which way the Parallel message runs is not known, and a number that is no message at all may be
  // somebody's question, so neither proves anything.
  EXPECT_FALSE(device_sent_message(PARALLEL_MESSAGE_TYPE));
  EXPECT_FALSE(device_sent_message(0x3A));
}

// The vendor's own library accepts exactly these numbers and refuses every other one, so this is
// the whole of the protocol's message space and not a sample of it.
TEST_F(MasterbusTest, TheComponentNamesTheMessagesTheVendorLibraryAcceptsAndNoOthers) {
  const std::set<uint8_t> accepted{0x02, 0x04, 0x05, 0x06, 0x08, 0x0A, 0x0C, 0x0D, 0x0E, 0x10, 0x11, 0x12,
                                   0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1B, 0x1D, 0x20, 0x21, 0x22,
                                   0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2B, 0x2D, 0x30, 0x31, 0x32,
                                   0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3B, 0x3D};
  for (uint8_t type = 0; type < 64; type++)
    EXPECT_EQ(is_known_message_type(type), accepted.count(type) == 1) << "type 0x" << std::hex << +type;
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
  EXPECT_EQ(this->canbus_.sent[0].can_id, 0x186C1001);
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
  EXPECT_EQ(this->canbus_.sent[0].can_id, 0x186C1001);
}

TEST_F(MasterbusTest, WritingAnEventSendsTheValueAndThenWhoWroteIt) {
  // Captured from the vendor library closing a battery relay: the write is a monitoring request
  // carrying a float, and a second write to the next field number follows it, carrying the
  // writer's own address.
  auto *relay = this->add_sensor(117);
  relay->on_eventable_answer(true);

  ASSERT_TRUE(this->hub_->write_value(*relay, 1.0f));

  ASSERT_EQ(this->canbus_.sent.size(), 2u);
  const auto &write = this->canbus_.sent[0];
  EXPECT_EQ(write.can_id, 0x186C1001);
  ASSERT_EQ(write.can_data_length_code, MONITORING_WRITE_LENGTH);
  EXPECT_EQ(encode_uint16(write.data[1], write.data[0]), 117);
  EXPECT_EQ(encode_uint32(write.data[5], write.data[4], write.data[3], write.data[2]), 0x3F800000);

  const auto &source = this->canbus_.sent[1];
  EXPECT_EQ(source.can_id, 0x186C1001);
  ASSERT_EQ(source.can_data_length_code, MONITORING_WRITE_LENGTH);
  EXPECT_EQ(encode_uint16(source.data[1], source.data[0]), 118);
  // The bytes the capture holds, and what they are.
  EXPECT_EQ(source.data[2], 0x01);
  EXPECT_EQ(source.data[3], 0x00);
  EXPECT_EQ(source.data[4], 0x50);
  EXPECT_EQ(source.data[5], 0x00);
  EXPECT_EQ(encode_uint32(source.data[5], source.data[4], source.data[3], source.data[2]), HUB_ADDRESS);
}

// A field that is no event has a field of its own one number up - a display panel's Backlight is 42
// and its Page duration 43 - so the write is the value alone.
TEST_F(MasterbusTest, AWriteToAFieldThatIsNoEventLeavesTheNextFieldAlone) {
  auto *backlight = this->add_sensor(42);
  backlight->on_eventable_answer(false);

  ASSERT_TRUE(this->hub_->write_value(*backlight, 70.0f));

  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  EXPECT_EQ(encode_uint16(this->canbus_.sent[0].data[1], this->canbus_.sent[0].data[0]), 42);
}

// The vendor library writes every tab the way it writes monitoring: the tab's own request number
// and the value.
TEST_F(MasterbusTest, AFieldOnAnyTabIsWrittenOnThatTabsOwnRequestNumber) {
  auto *setting =
      this->add_sensor(40, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

  ASSERT_TRUE(this->hub_->write_value(*setting, 13.5f));

  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  const auto &write = this->canbus_.sent[0];
  EXPECT_EQ(write.can_id, frame_id(0x37, BATTERY_1));
  ASSERT_EQ(write.can_data_length_code, MONITORING_WRITE_LENGTH);
  EXPECT_EQ(encode_uint16(write.data[1], write.data[0]), 40);
  float value;
  const uint32_t bits = encode_uint32(write.data[5], write.data[4], write.data[3], write.data[2]);
  memcpy(&value, &bits, sizeof(value));
  EXPECT_FLOAT_EQ(value, 13.5f);

  for (MasterbusTab tab : MASTERBUS_TABS) {
    this->canbus_.clear();
    auto *field = this->add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN, tab);
    ASSERT_TRUE(this->hub_->write_value(*field, 0.0f));
    ASSERT_EQ(this->canbus_.sent.size(), 1u);
    EXPECT_EQ(this->canbus_.sent[0].can_id >> MESSAGE_TYPE_SHIFT,
              tab_message(tab, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA).request)
        << LOG_STR_ARG(masterbus_tab_to_string(tab));
  }
}

TEST_F(MasterbusTest, AnAlarmFieldIsPolledAndAnsweredOnItsOwnMessageNumbers) {
  auto *alarm = this->add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_ALARM);

  alarm->update();
  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  EXPECT_EQ(this->canbus_.sent[0].can_id >> MESSAGE_TYPE_SHIFT, 0x06);

  this->hub_->on_frame(frame_id(0x04, BATTERY_1), true, false, monitoring_answer(1, 1.0f));
  ASSERT_EQ(alarm->values.size(), 1u);
  EXPECT_FLOAT_EQ(alarm->values[0].as_float, 1.0f);
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
  // 0x046C1001 is the identifier BAT 1 announces itself under.
  ASSERT_EQ(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), 0x046C1001u);
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_6), true, false,
                       {0x1B, 0x02, 0x10, 0x00, 0x95, 0x03, 0x00, 0x04});

  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[0].address, BATTERY_1);
  EXPECT_EQ(found[1].address, BATTERY_6);
}

TEST_F(MasterbusTest, ScanFindsDevicesNobodyDeclared) {
  // 0x501001 is a display panel that was never configured. Discovery must still list it.
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, 0x501001), true, false,
                       {0x14, 0x01, 0x10, 0x00, 0x12, 0x01, 0x00, 0x00});

  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].address, 0x501001u);
}

TEST_F(MasterbusTest, AnnouncementKeepsToTheAddressBitsTheIdentifierCarries) {
  // The first payload byte has room for six bits, but sixteen plus two plus five is the 23 bits
  // the identifier gives the address. Shifting a sixth bit in would put the device above the
  // range any configuration can declare, so it could never be matched again.
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x3B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});

  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_LE(found[0].address, MAX_DEVICE_ADDRESS);
  EXPECT_EQ(found[0].address, BATTERY_1);
}

TEST_F(MasterbusTest, RepeatedAnnouncementsCountRatherThanDuplicate) {
  for (int i = 0; i < 3; i++) {
    this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                         {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  }

  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].announcements, 3u);
}

// A scan started on demand lists what answers it, not what announced itself before.
TEST_F(MasterbusTest, AScanStartedOnDemandForgetsTheDevicesHeardBefore) {
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  this->hub_->start_scan();
  EXPECT_TRUE(this->hub_->get_discovered_devices().empty());

  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_6), true, false,
                       {0x1B, 0x02, 0x10, 0x00, 0x95, 0x03, 0x00, 0x04});
  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].address, BATTERY_6);
}

// The walk reads the device list as it goes, so a second start while it runs leaves it alone.
TEST_F(MasterbusTest, AScanThatIsWalkingIsNotStartedAgain) {
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();
  this->hub_->start_scan();

  ASSERT_EQ(this->hub_->get_discovered_devices().size(), 1u);
  EXPECT_EQ(this->hub_->get_discovered_devices()[0].address, BATTERY_1);
}

TEST_F(MasterbusTest, ReportingTheScanTransmitsNothing) {
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();

  EXPECT_TRUE(this->canbus_.sent.empty());
}

TEST_F(MasterbusTest, NodeRequestIsAnEmptyFrameUnderTheVendorsOwnAddress) {
  // The whole message is the identifier: the node request type against the address of the node
  // asking, which here is the one the vendor library uses for itself.
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

// A hub that can say which timers it has armed.
class TimerHub : public MasterbusHub {
 public:
  using MasterbusHub::MasterbusHub;
  using Component::cancel_interval;
};

// node_request_interval compiles node requests in for every hub. A second hub given no interval of
// its own must not arm one at zero, which the scheduler runs every millisecond.
TEST_F(MasterbusTest, OnlyAHubGivenAnIntervalAsksForAnnouncements) {
  TimerHub quiet(&this->canbus_);
  TimerHub asking(&this->canbus_);
  asking.set_node_request_interval(10000);
  quiet.setup();
  asking.setup();

  EXPECT_FALSE(quiet.cancel_interval(NODE_REQUEST_INTERVAL_ID));
  EXPECT_TRUE(asking.cancel_interval(NODE_REQUEST_INTERVAL_ID));
  quiet.cancel_interval(AVAILABILITY_INTERVAL_ID);
  asking.cancel_interval(AVAILABILITY_INTERVAL_ID);
}

TEST_F(MasterbusTest, ScanCollectsWhatTheNodeRequestBringsBack) {
  this->hub_->request_nodes();
  // Every device answers within a few milliseconds; two of the eight recorded on the real bus.
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->feed("ext 0x04501001 [8] 14:01:10:00:12:01:00:00");

  const auto &found = this->hub_->get_discovered_devices();
  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[0].address, BATTERY_1);
  EXPECT_EQ(found[1].address, 0x501001u);
}

// The scan's second half: walking a device for its fields. Frames below are shaped the way the
// bus shapes them, with the values the vendor library reported for the same field.
TEST_F(MasterbusTest, ScanAsksForAGroupsFieldCount) {
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  this->canbus_.clear();

  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();
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
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();

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
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();

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
  this->unanswered_factory_and_flags();

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
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();

  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});  // group has no name
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});  // name string 97
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x0A, 0x00});  // unit string 10
  this->answer(PROPERTY_INFORMATION_TYPE, {0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x16, 0x44});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x08, 0x01, 0x00, 0x00, 0x0A, 0xD7, 0x23, 0x3C});
  this->unanswered_factory_and_flags();

  // A chunk of string 98 arrives in the middle; it belongs to nobody here.
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x62, 0x00, 0x00, 'Z', 'Z', 'Z', 'Z'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});

  EXPECT_STREQ(this->hub_->get_scanned_field().name, "Battery");
}

// The same in the walk: another node's answer to a later chunk of the name being read is not the
// end of it.
TEST_F(MasterbusTest, AnotherNodesLaterChunkDoesNotEndAStringTheScanReads) {
  this->hub_->on_frame(frame_id(DEVICE_ANNOUNCEMENT_TYPE, BATTERY_1), true, false,
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();

  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});  // group has no name
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});  // name string 97
  this->unanswered();                                                             // no unit
  this->unanswered();                                                             // no minimum
  this->unanswered();                                                             // no maximum
  this->unanswered();                                                             // no step
  this->unanswered_factory_and_flags();

  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});

  EXPECT_STREQ(this->hub_->get_scanned_field().name, "Battery");
}

// A display panel walking the same device during a scan is answered on the same bus, in the same
// shape. Each answer is taken only if it carries the selector, the group or field number and, where
// there is one, the index the walk asked for; anything else is somebody else's.
TEST_F(MasterbusTest, TheScanTakesOnlyTheAnswerToItsOwnQuestion) {
  // Before the real answer, the same answer about another group or field, under another selector,
  // and, where the question had an index, at another index. Each with a value of its own, so that
  // taking it shows.
  const auto answer_after_others = [this](uint8_t type, const std::vector<uint8_t> &data, bool indexed = false) {
    this->hub_->scan_step(this->scan_now_);
    std::vector<std::vector<uint8_t>> others(indexed ? 3 : 2, data);
    others[0][1] ^= 0x04;
    others[1][0] ^= 0x40;
    if (indexed)
      others[2][3] ^= 0x01;
    for (auto &other : others) {
      other[4] ^= 0x22;
      this->hub_->on_frame(frame_id(type, BATTERY_1), true, false, other);
    }
    this->hub_->on_frame(frame_id(type, BATTERY_1), true, false, data);
  };
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();

  answer_after_others(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F});  // one field
  answer_after_others(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x50, 0x00});              // named by string 80
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x50, 0x00, 0x00, 'B', 'a', 'n', 'k'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x50, 0x00, 0x01, 0x00});
  answer_after_others(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00}, true);           // field 1
  answer_after_others(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});              // a float
  answer_after_others(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});              // name 97
  answer_after_others(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x0A, 0x00});              // unit 10
  answer_after_others(PROPERTY_INFORMATION_TYPE, {0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // 0
  answer_after_others(PROPERTY_INFORMATION_TYPE, {0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x16, 0x44});  // 600
  answer_after_others(PROPERTY_INFORMATION_TYPE, {0x08, 0x01, 0x00, 0x00, 0x0A, 0xD7, 0x23, 0x3C});  // 0.01
  answer_after_others(PROPERTY_INFORMATION_TYPE, {0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x48, 0x42});  // 50
  answer_after_others(PROPERTY_INFORMATION_TYPE, {0x0B, 0x01, 0x00, 0x00, 0x00, 0x00});              // read only
  answer_after_others(PROPERTY_INFORMATION_TYPE, {0x0D, 0x01, 0x00, 0x00, 0x00, 0x00});              // no event
  this->unanswered();                                                                                // no long name
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x0A, 0x00, 0x00, 'V', 0x00});

  EXPECT_STREQ(this->hub_->get_scanned_group_name(), "Bank");
  const auto &field = this->hub_->get_scanned_field();
  EXPECT_EQ(field.group, 0);
  EXPECT_EQ(field.param, 1);
  EXPECT_EQ(field.display_type, MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_FLOAT);
  EXPECT_STREQ(field.name, "Battery");
  EXPECT_STREQ(field.unit, "V");
  EXPECT_EQ(field.minimum, 0.0f);
  EXPECT_EQ(field.maximum, 600.0f);
  EXPECT_FLOAT_EQ(field.step, 0.01f);
  EXPECT_EQ(field.factory, 50.0f);
  EXPECT_EQ(field.writable, 0);
  EXPECT_EQ(field.eventable, 0);
}

// The list walks echo their index already; the field number is checked as well.
TEST_F(MasterbusTest, AnOptionAnswerForAnotherFieldIsNotTaken) {
  this->walk_to_list_field(MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_DROPDOWN, 2.0f);
  this->hub_->scan_step(this->scan_now_);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false, {0x26, 13, 0x00, 0x00, 0x70, 0x00});
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false, {0x26, 12, 0x00, 0x00, 0x71, 0x00});
  this->hub_->scan_step(this->scan_now_);
  const auto &asked = this->canbus_.sent.back();
  EXPECT_EQ(asked.can_id >> MESSAGE_TYPE_SHIFT, STRING_REQUEST_TYPE);
  EXPECT_EQ(encode_uint16(asked.data[2], asked.data[1]), 0x71);
}

// The vendor's own message list, read out of its shared library. Six of the twelve entries are
// pinned by traffic; the rest follow from the same ordering. These assert the pinned ones, because
// if the ordering is ever edited it is those that must not move.
TEST_F(MasterbusTest, MessageNumbersMatchTheTrafficThatPinsThem) {
  using M = MasterbusTabMessage;
  using T = MasterbusTab;
  EXPECT_EQ(tab_message(T::MASTERBUS_TAB_MONITORING, M::MASTERBUS_TAB_MESSAGE_DATA).request, 0x30);
  EXPECT_EQ(tab_message(T::MASTERBUS_TAB_MONITORING, M::MASTERBUS_TAB_MESSAGE_DATA).information, 0x10);
  EXPECT_EQ(tab_message(T::MASTERBUS_TAB_MONITORING, M::MASTERBUS_TAB_MESSAGE_PROPERTY).request, 0x31);
  EXPECT_EQ(tab_message(T::MASTERBUS_TAB_MONITORING, M::MASTERBUS_TAB_MESSAGE_GROUP).request, 0x32);
  // Seen on an unrelated installation: a display panel walking the other tabs' group lists.
  EXPECT_EQ(tab_message(T::MASTERBUS_TAB_ALARM, M::MASTERBUS_TAB_MESSAGE_GROUP).request, 0x34);
  EXPECT_EQ(tab_message(T::MASTERBUS_TAB_ALARM, M::MASTERBUS_TAB_MESSAGE_GROUP).information, 0x14);
  EXPECT_EQ(tab_message(T::MASTERBUS_TAB_HISTORY, M::MASTERBUS_TAB_MESSAGE_GROUP).request, 0x36);
  EXPECT_EQ(tab_message(T::MASTERBUS_TAB_HISTORY, M::MASTERBUS_TAB_MESSAGE_GROUP).information, 0x16);
  EXPECT_EQ(tab_message(T::MASTERBUS_TAB_CONFIGURATION, M::MASTERBUS_TAB_MESSAGE_GROUP).request, 0x39);
  EXPECT_EQ(tab_message(T::MASTERBUS_TAB_CONFIGURATION, M::MASTERBUS_TAB_MESSAGE_GROUP).information, 0x19);
}

TEST_F(MasterbusTest, EveryTabHasAValueMessageOfItsOwn) {
  // Values are the one kind of tab message that follows no rule: the vendor library looks the
  // numbers up, and alarm values sit down among the node messages, far from the other tabs.
  const auto value = [](MasterbusTab tab) { return tab_message(tab, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA); };
  EXPECT_EQ(value(MasterbusTab::MASTERBUS_TAB_MONITORING).request, 0x30);
  EXPECT_EQ(value(MasterbusTab::MASTERBUS_TAB_ALARM).request, 0x06);
  EXPECT_EQ(value(MasterbusTab::MASTERBUS_TAB_ALARM).information, 0x04);
  EXPECT_EQ(value(MasterbusTab::MASTERBUS_TAB_ALARM).not_available, 0x05);
  EXPECT_EQ(value(MasterbusTab::MASTERBUS_TAB_HISTORY).request, 0x3B);
  EXPECT_EQ(value(MasterbusTab::MASTERBUS_TAB_HISTORY).information, 0x1B);
  EXPECT_EQ(value(MasterbusTab::MASTERBUS_TAB_HISTORY).not_available, 0x2B);
  EXPECT_EQ(value(MasterbusTab::MASTERBUS_TAB_CONFIGURATION).request, 0x37);
}

TEST_F(MasterbusTest, AHistoryFieldIsPolledAndAnsweredOnItsOwnMessageNumbers) {
  auto *history =
      this->add_sensor(5, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_HISTORY);
  auto *monitoring = this->add_sensor(5);

  history->update();
  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  EXPECT_EQ(this->canbus_.sent[0].can_id >> MESSAGE_TYPE_SHIFT, 0x3B);
  ASSERT_EQ(this->canbus_.sent[0].can_data_length_code, MONITORING_REQUEST_LENGTH);

  this->hub_->on_frame(frame_id(0x1B, BATTERY_1), true, false, monitoring_answer(5, 812.0f));
  ASSERT_EQ(history->values.size(), 1u);
  EXPECT_FLOAT_EQ(history->values[0].as_float, 812.0f);
  EXPECT_TRUE(monitoring->values.empty());

  // 0x1A was once taken for history's answer. The vendor library refuses it, so nothing takes it.
  this->hub_->on_frame(frame_id(0x1A, BATTERY_1), true, false, monitoring_answer(5, 1.0f));
  EXPECT_EQ(history->values.size(), 1u);
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
                       {0x1B, 0x01, 0x10, 0x00, 0x51, 0x00, 0x00, 0x02});
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();
  this->hub_->scan_step(this->scan_now_);
  const size_t asked = this->canbus_.sent.size();

  // The walk starts on monitoring and is waiting for 0x12. This is the alarm tab's answer.
  this->hub_->on_frame(
      frame_id(
          tab_message(MasterbusTab::MASTERBUS_TAB_ALARM, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_GROUP).information,
          BATTERY_1),
      true, false, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
  this->hub_->scan_step(this->scan_now_);
  EXPECT_EQ(this->canbus_.sent.size(), asked) << "the scan moved on after an answer from another tab";

  // The same payload under monitoring's own number is taken, and the walk asks its next question.
  this->hub_->on_frame(
      frame_id(tab_message(MasterbusTab::MASTERBUS_TAB_MONITORING, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_GROUP)
                   .information,
               BATTERY_1),
      true, false, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x40});
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

  // 0x3A is no message at all: the vendor library refuses it. UNDECLARED_DEVICE is deliberate: a
  // device-level trigger could not have seen this at all.
  this->hub_->on_frame(frame_id(0x3A, UNDECLARED_DEVICE), true, false, {0x1B, 0x01, 0x10});

  ASSERT_EQ(seen.size(), 1u);
  EXPECT_EQ(std::get<1>(seen[0]), 0x3A);
  EXPECT_EQ(std::get<2>(seen[0]), UNDECLARED_DEVICE);
  EXPECT_EQ(std::get<0>(seen[0]), (std::vector<uint8_t>{0x1B, 0x01, 0x10}));
}

TEST_F(MasterbusTest, EveryMessageTheComponentCanNameIsLeftAlone) {
  int fired = 0;
  this->hub_->add_on_unknown_frame_callback([&fired](const std::vector<uint8_t> &, uint8_t, uint32_t) { fired++; });

  // Every tab message in all three of its forms, then node and string. A value read or its answer
  // is sent in the shape it has on the wire, because a value message of another length is
  // something else (see below); the rest are not judged by their length.
  for (MasterbusTab tab : MASTERBUS_TABS) {
    for (MasterbusTabMessage message : MASTERBUS_TAB_MESSAGES) {
      const MasterbusMessageNumbers numbers = tab_message(tab, message);
      const bool value = message == MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA;
      this->hub_->on_frame(frame_id(numbers.request, BATTERY_1), true, false,
                           value ? std::vector<uint8_t>{0x75, 0x00} : std::vector<uint8_t>{0x00});
      this->hub_->on_frame(
          frame_id(numbers.information, BATTERY_1), true, false,
          value ? std::vector<uint8_t>{0x75, 0x00, 0x00, 0x00, 0x80, 0x3F} : std::vector<uint8_t>{0x00});
      this->hub_->on_frame(frame_id(numbers.not_available, BATTERY_1), true, false, {0x00});
    }
  }
  for (uint8_t type : {DEVICE_ANNOUNCEMENT_TYPE, NODE_REQUEST_TYPE, STRING_INFORMATION_TYPE, STRING_NOT_AVAILABLE_TYPE,
                       STRING_REQUEST_TYPE})
    this->hub_->on_frame(frame_id(type, BATTERY_1), true, false, {0x00});

  EXPECT_EQ(fired, 0);

  // And a number either side of the tab block, so the bounds are not off by one.
  this->hub_->on_frame(frame_id(0x0F, BATTERY_1), true, false, {0x00});
  this->hub_->on_frame(frame_id(0x3E, BATTERY_1), true, false, {0x00});
  EXPECT_EQ(fired, 2);
}

// The Parallel message and the bootloader's are numbers the vendor library accepts and then never
// looks inside, and this component reads nothing in them either. Neither is everyday traffic, which is
// what makes one worth seeing.
TEST_F(MasterbusTest, TheMessagesNobodyParsesReachTheHubTrigger) {
  std::vector<uint8_t> seen;
  this->hub_->add_on_unknown_frame_callback(
      [&seen](const std::vector<uint8_t> &, uint8_t type, uint32_t) { seen.push_back(type); });

  for (uint8_t type : {PARALLEL_MESSAGE_TYPE, BOOTLOADER_MESSAGE.request, BOOTLOADER_MESSAGE.information,
                       BOOTLOADER_MESSAGE.not_available})
    this->hub_->on_frame(frame_id(type, UNDECLARED_DEVICE), true, false, {0x00});

  EXPECT_EQ(seen, (std::vector<uint8_t>{0x02, 0x3D, 0x1D, 0x2D}));
}

// A write rides the same message as a read and is told apart by its length alone, and the frame
// that follows it is shorter again. This controller never hears its own frames, so a write seen
// here was sent by some other node - a device carrying out one of its events, or a display panel
// someone pressed - and that is exactly the traffic nothing else here reads.
TEST_F(MasterbusTest, AWriteByAnotherNodeAndTheFrameAfterItReachTheHubTrigger) {
  std::vector<std::tuple<std::vector<uint8_t>, uint8_t, uint32_t>> seen;
  this->hub_->add_on_unknown_frame_callback([&seen](const std::vector<uint8_t> &data, uint8_t type, uint32_t device) {
    seen.emplace_back(data, type, device);
  });

  // Field 117 set to 1.0, then field 118 set to the writer's address, both aimed at the device
  // being written to.
  this->hub_->on_frame(frame_id(MONITORING_REQUEST_TYPE, UNDECLARED_DEVICE), true, false,
                       {0x75, 0x00, 0x00, 0x00, 0x80, 0x3F});
  this->hub_->on_frame(frame_id(MONITORING_REQUEST_TYPE, UNDECLARED_DEVICE), true, false,
                       {0x76, 0x00, 0x01, 0x00, 0x50, 0x00});
  // The same shape on the configuration tab's message: no write there has been watched, which is
  // all the more reason to report one.
  const uint8_t configuration_request =
      tab_message(MasterbusTab::MASTERBUS_TAB_CONFIGURATION, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA).request;
  this->hub_->on_frame(frame_id(configuration_request, BATTERY_1), true, false, {0x28, 0x00, 0x00, 0x00, 0xA0, 0x40});

  ASSERT_EQ(seen.size(), 3u);
  EXPECT_EQ(std::get<1>(seen[0]), MONITORING_REQUEST_TYPE);
  EXPECT_EQ(std::get<2>(seen[0]), UNDECLARED_DEVICE) << "the address is the one written to";
  EXPECT_EQ(std::get<0>(seen[1]), (std::vector<uint8_t>{0x76, 0x00, 0x01, 0x00, 0x50, 0x00}));
  EXPECT_EQ(std::get<1>(seen[2]), configuration_request);
}

// An answer to a value read that is not a field number and a float is a message this component
// does not understand, whoever it came from, and it is said rather than dropped.
TEST_F(MasterbusTest, AValueAnswerOfAnotherLengthReachesTheHubTrigger) {
  int fired = 0;
  this->hub_->add_on_unknown_frame_callback([&fired](const std::vector<uint8_t> &, uint8_t, uint32_t) { fired++; });

  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, UNDECLARED_DEVICE), true, false, {0x75, 0x00, 0x01});
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x75, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00});
  EXPECT_EQ(fired, 2);

  // Another node polling, and the answers it gets, are what a bus with a display on it carries all
  // day. None of it is news.
  this->hub_->on_frame(frame_id(MONITORING_REQUEST_TYPE, UNDECLARED_DEVICE), true, false, {0x75, 0x00});
  this->hub_->on_frame(frame_id(MONITORING_INFORMATION_TYPE, UNDECLARED_DEVICE), true, false,
                       {0x75, 0x00, 0x00, 0x00, 0x80, 0x3F});
  EXPECT_EQ(fired, 2);
}

// The scan's lines are what the converter on the documentation page reads, so they are pinned
// exactly: a device, a named group, and one field the device described completely.
TEST_F(MasterbusTest, AScanReportsWhatTheDeviceAnsweredAsKeyAndValue) {
  scan_lines().clear();
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();

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
  // Factory value 13.8, then writeable set and eventable clear: a setting, not a button.
  this->answer(PROPERTY_INFORMATION_TYPE, {0x09, 0x01, 0x00, 0x00, 0xCD, 0xCC, 0x5C, 0x41});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x0B, 0x01, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x0D, 0x01, 0x00, 0x00, 0x00, 0x00});
  this->unanswered();  // no long name
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 't'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x01, 'e', 'r', 'y', 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x0A, 0x00, 0x00, 'V', 0x00});

  ASSERT_EQ(scan_lines().lines().size(), 3u);
  EXPECT_EQ(scan_lines().lines()[0], "device device=0x6C1001 class=27 eventables=2");
  EXPECT_EQ(scan_lines().lines()[1], "group device=0x6C1001 tab=0 group=0 name=\"Bank\"");
  EXPECT_EQ(scan_lines().lines()[2],
            "field device=0x6C1001 tab=0 group=0 param=1 display=1 name=\"Battery\" unit=\"V\" min=0 max=60 "
            "step=0.1 factory=13.8 writable=1 eventable=0");
}

// What a device did not answer for is absent, rather than reported as a value it never sent.
TEST_F(MasterbusTest, AFieldTheDeviceBarelyDescribesReportsOnlyWhatItAnswered) {
  scan_lines().clear();
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();

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
  this->unanswered_factory_and_flags();

  ASSERT_EQ(scan_lines().lines().size(), 3u);
  EXPECT_EQ(scan_lines().lines()[1], "group device=0x6C1001 tab=0 group=0");
  EXPECT_EQ(scan_lines().lines()[2], "field device=0x6C1001 tab=0 group=0 param=117 display=5");
}

// Asking the string table for entry zero would cost an answer timeout on every field that
// carries no unit, and most of them do not.
TEST_F(MasterbusTest, AFieldWithNoUnitIsNotAskedForTheUnitString) {
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();

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
  this->unanswered_factory_and_flags();
  this->hub_->scan_step(this->scan_now_);

  for (const auto &frame : this->canbus_.sent)
    EXPECT_NE(frame.can_id >> MESSAGE_TYPE_SHIFT, STRING_REQUEST_TYPE)
        << "the walk asked the string table for a field that carries no strings";
}

// The flag saying a group carries a name belongs to the group being walked, not to the last one
// that had one.
TEST_F(MasterbusTest, AGroupThatDoesNotAnswerForItsNameReportsNoNameAtAll) {
  scan_lines().clear();
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->refuse_group_count();

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
  this->unanswered_factory_and_flags();

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
  this->unanswered_factory_and_flags();

  ASSERT_EQ(scan_lines().lines().size(), 5u);
  EXPECT_EQ(scan_lines().lines()[1], "group device=0x6C1001 tab=0 group=0 name=\"Bank\"");
  EXPECT_EQ(scan_lines().lines()[3], "group device=0x6C1001 tab=0 group=1");
}

// The question every device is asked first: how many event slots it has. Six identical battery
// blocks on the reference bus all answered 2 and the display panel 16. What kind of device it is
// needs no question at all - 0x6C1001 is class 27, a battery monitor, by its address alone.
TEST_F(MasterbusTest, ScanReportsTheDevicesClassAndItsEventables) {
  scan_lines().clear();
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->canbus_.clear();
  this->hub_->report_scan();
  this->hub_->scan_step(this->scan_now_);

  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  const auto &asked = this->canbus_.sent.back();
  EXPECT_EQ(asked.can_id >> MESSAGE_TYPE_SHIFT, STRING_REQUEST_TYPE);
  EXPECT_EQ(asked.can_id & DEVICE_ADDRESS_MASK, BATTERY_1);
  ASSERT_EQ(asked.can_data_length_code, GENERAL_COUNT_REQUEST_LENGTH);
  EXPECT_EQ(asked.data[0], GENERAL_COUNT_SELECTOR);
  EXPECT_EQ(asked.data[1], GENERAL_COUNT_EVENTABLES);

  // 08:3f:02:00 - the answer a lithium battery block gives, recorded off the bus.
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {GENERAL_COUNT_SELECTOR, GENERAL_COUNT_EVENTABLES, 0x02, 0x00});
  // The line waits for the device's strings and its processors, asked next.
  this->refuse_device_strings();
  EXPECT_TRUE(scan_lines().lines().empty());
  this->refuse_software_version();

  ASSERT_EQ(scan_lines().lines().size(), 1u);
  EXPECT_EQ(scan_lines().lines()[0], "device device=0x6C1001 class=27 eventables=2");
}

// A device's article number, serial number, product name and revision code are strings in its own
// table, and a GeneralString question - 1 to 4, in that order - says which entry holds each. Then
// each processor's software version, until the device refuses the one past its last. The serial
// number here is the vendor's own made-up one, from its example device.
TEST_F(MasterbusTest, ScanReportsTheDevicesOwnStringsAndItsFirmware) {
  scan_lines().clear();
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer(STRING_INFORMATION_TYPE, {GENERAL_COUNT_SELECTOR, GENERAL_COUNT_EVENTABLES, 0x02, 0x00});

  // Article number: string 2.
  this->hub_->scan_step(this->scan_now_);
  ASSERT_EQ(this->canbus_.sent.back().can_id >> MESSAGE_TYPE_SHIFT, STRING_REQUEST_TYPE);
  ASSERT_EQ(this->canbus_.sent.back().can_data_length_code, 2);
  EXPECT_EQ(this->canbus_.sent.back().data[0], GENERAL_STRING_SELECTOR);
  EXPECT_EQ(this->canbus_.sent.back().data[1], GENERAL_STRING_ARTICLE_NUMBER);
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {GENERAL_STRING_SELECTOR, GENERAL_STRING_ARTICLE_NUMBER, 0x02, 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x02, 0x00, 0x00, '6', '6', '0', '2'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x02, 0x00, 0x01, '5', '0', '0', '0'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x02, 0x00, 0x02, 0x00});
  // Serial number: string 0, which is a real entry here.
  this->hub_->scan_step(this->scan_now_);
  EXPECT_EQ(this->canbus_.sent.back().data[1], GENERAL_STRING_SERIAL_NUMBER);
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {GENERAL_STRING_SELECTOR, GENERAL_STRING_SERIAL_NUMBER, 0x00, 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x00, 0x00, 0x00, 'A', 'B', '0', '0'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x00, 0x00, 0x01, 'A', '0', '0', '0'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x00, 0x00, 0x02, '1'});
  // Product name: string 1.
  this->hub_->scan_step(this->scan_now_);
  EXPECT_EQ(this->canbus_.sent.back().data[1], GENERAL_STRING_PRODUCT_NAME);
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {GENERAL_STRING_SELECTOR, GENERAL_STRING_PRODUCT_NAME, 0x01, 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x01, 0x00, 0x00, 'M', 'L', 'I', 0x00});
  // No revision code.
  this->hub_->scan_step(this->scan_now_);
  EXPECT_EQ(this->canbus_.sent.back().data[1], GENERAL_STRING_REVISION_CODE);
  this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false,
                       {GENERAL_STRING_SELECTOR, GENERAL_STRING_REVISION_CODE});

  // One processor, at 61.1 as the vendor library prints 82:00:01:3D, and none after it.
  this->hub_->scan_step(this->scan_now_);
  ASSERT_EQ(this->canbus_.sent.back().can_data_length_code, 2);
  EXPECT_EQ(this->canbus_.sent.back().data[0], SOFTWARE_VERSION_SELECTOR);
  EXPECT_EQ(this->canbus_.sent.back().data[1], 0);
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {SOFTWARE_VERSION_SELECTOR, 0x00, 0x01, 0x3D, 0x00, 0x00});
  this->hub_->scan_step(this->scan_now_);
  EXPECT_EQ(this->canbus_.sent.back().data[1], 1);
  this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false, {SOFTWARE_VERSION_SELECTOR, 0x01});

  ASSERT_EQ(scan_lines().lines().size(), 1u);
  EXPECT_EQ(scan_lines().lines()[0], "device device=0x6C1001 class=27 eventables=2 article=\"66025000\" "
                                     "serial=\"AB00A0001\" name=\"MLI\" firmware=\"61.1\"");
}

// Equipment that does not carry eventables refuses the question outright - one device on the
// reference bus refused all 240 of them. It is still listed, and its fields are still walked.
TEST_F(MasterbusTest, ADeviceThatRefusesEventablesIsStillListedAndWalked) {
  scan_lines().clear();
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->hub_->scan_step(this->scan_now_);
  this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false,
                       {GENERAL_COUNT_SELECTOR, GENERAL_COUNT_EVENTABLES});
  this->refuse_device_strings();
  this->refuse_software_version();

  ASSERT_EQ(scan_lines().lines().size(), 1u);
  EXPECT_EQ(scan_lines().lines()[0], "device device=0x6C1001 class=27");

  // And the walk carried on: the round of counts, then how many groups the tab holds, then the
  // group itself.
  this->refuse_general_counts();
  this->canbus_.clear();
  this->hub_->scan_step(this->scan_now_);
  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  EXPECT_EQ(this->canbus_.sent.back().can_id >> MESSAGE_TYPE_SHIFT, STRING_REQUEST_TYPE);
  EXPECT_EQ(this->canbus_.sent.back().data[1], GENERAL_COUNT_MONITORING_GROUPS);

  this->refuse_group_count();
  this->canbus_.clear();
  this->hub_->scan_step(this->scan_now_);
  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  EXPECT_EQ(this->canbus_.sent.back().can_id >> MESSAGE_TYPE_SHIFT, GROUP_REQUEST_TYPE);
}

// Every count the vendor library accepts is asked once, in order, and each answer is reported as
// it stands: what most of them mean is the vendor's name for them, not something seen on a bus.
// The ones the walk asks for its own use are not asked twice.
TEST_F(MasterbusTest, ScanAsksEveryCountOnceAndReportsWhatWasAnswered) {
  scan_lines().clear();
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer(STRING_INFORMATION_TYPE, {GENERAL_COUNT_SELECTOR, GENERAL_COUNT_EVENTABLES, 0x02, 0x00});
  this->refuse_device_strings();
  this->refuse_software_version();
  scan_lines().clear();

  std::vector<uint8_t> asked;
  for (unsigned i = 0; i < general_count_round(); i++) {
    this->hub_->scan_step(this->scan_now_);
    const auto &frame = this->canbus_.sent.back();
    ASSERT_EQ(frame.data[0], GENERAL_COUNT_SELECTOR);
    asked.push_back(frame.data[1]);
    if (frame.data[1] == 0x0B) {
      // SoftwareVersion, by the vendor's name.
      this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                           {GENERAL_COUNT_SELECTOR, 0x0B, 0x3D, 0x00});
    } else {
      this->hub_->on_frame(frame_id(STRING_NOT_AVAILABLE_TYPE, BATTERY_1), true, false,
                           {GENERAL_COUNT_SELECTOR, frame.data[1]});
    }
  }

  EXPECT_TRUE(std::is_sorted(asked.begin(), asked.end()));
  for (uint8_t question : asked) {
    EXPECT_TRUE(is_general_count_question(question)) << "asked 0x" << std::hex << unsigned{question};
    EXPECT_NE(question, GENERAL_COUNT_EVENTABLES);
    EXPECT_NE(question, GENERAL_COUNT_MONITORING_GROUPS);
  }
  EXPECT_EQ(asked.front(), 0x01);
  EXPECT_EQ(asked.back(), GENERAL_COUNT_LAST_QUESTION);
  ASSERT_EQ(scan_lines().lines().size(), 1u);
  EXPECT_EQ(scan_lines().lines()[0], "count device=0x6C1001 q=0x0B value=61");
}

// A string chunk and a general count arrive under the same message number, so the first byte is
// what tells them apart. Reading a chunk as a code would report a product nobody makes.
TEST_F(MasterbusTest, AStringChunkIsNotReadAsAGeneralCount) {
  scan_lines().clear();
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->hub_->scan_step(this->scan_now_);
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {STRING_REQUEST_MARKER, 0x50, 0x00, 0x00, 'B', 'a', 'n', 'k'});

  EXPECT_TRUE(scan_lines().lines().empty()) << "a string chunk was taken for a general count";
}

// The group counts are what save the walk a question per tab and per group. A tab the device says
// holds nothing is skipped without asking about a group on it at all.
TEST_F(MasterbusTest, ATabTheDeviceSaysIsEmptyIsNotWalked) {
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer_eventables(2);

  this->canbus_.clear();
  this->answer_group_count(0);
  this->hub_->scan_step(this->scan_now_);

  ASSERT_EQ(this->canbus_.sent.size(), 2u) << "the walk asked about a group on an empty tab";
  const auto &next = this->canbus_.sent.back();
  EXPECT_EQ(next.can_id >> MESSAGE_TYPE_SHIFT, STRING_REQUEST_TYPE);
  EXPECT_EQ(next.data[1], GENERAL_COUNT_ALARM_GROUPS);
}

// And the walk stops at the last group the device counted, rather than finding the end by asking
// for one more group and one more field and waiting out both silences.
TEST_F(MasterbusTest, TheWalkStopsAtTheLastGroupAndFieldTheDeviceCounted) {
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->answer_group_count(1);

  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F});  // one field
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});              // no group name
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});              // index 0 is field 1
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x00, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x00, 0x00});
  this->unanswered();  // minimum
  this->unanswered();  // maximum
  this->unanswered();  // step
  this->unanswered_factory_and_flags();

  this->canbus_.clear();
  this->hub_->scan_step(this->scan_now_);

  ASSERT_EQ(this->canbus_.sent.size(), 1u);
  const auto &next = this->canbus_.sent.back();
  EXPECT_EQ(next.can_id >> MESSAGE_TYPE_SHIFT, STRING_REQUEST_TYPE)
      << "the walk asked for a group or a field the device had already counted out";
  EXPECT_EQ(next.data[1], GENERAL_COUNT_ALARM_GROUPS);
}

// A name longer than the buffer is reported for what arrived rather than for what was sent, and
// the component says so instead of passing the cut text off as the device's own.
TEST_F(MasterbusTest, ANameLongerThanTheBufferIsReportedCutShort) {
  scan_lines().clear();
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->answer_group_count(1);

  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F});
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});  // name is string 97
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x00, 0x00});
  this->unanswered();  // minimum
  this->unanswered();  // maximum
  this->unanswered();  // step
  this->unanswered_factory_and_flags();

  // Forty characters, where the buffer holds thirty-one and a terminator.
  for (uint8_t chunk = 0; chunk < 10; chunk++)
    this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, chunk, 'a', 'b', 'c', 'd'});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 10, 0x00});

  ASSERT_FALSE(scan_lines().lines().empty());
  const std::string &line = scan_lines().lines().back();
  EXPECT_NE(line.find("name=\"abcdabcdabcdabcdabcdabcdabcdabc\""), std::string::npos) << line;
}

// The walk stops asking for a string once the buffer is full, rather than going on for as long as a
// device answers.
TEST_F(MasterbusTest, TheScanStopsReadingAStringThatNeverEnds) {
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->answer_group_count(1);

  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F});
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});
  this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x02, 0x01, 0x00, 0x00, 0x01, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x28, 0x01, 0x00, 0x00, 0x61, 0x00});  // name is string 97
  this->answer(PROPERTY_INFORMATION_TYPE, {0x2C, 0x01, 0x00, 0x00, 0x00, 0x00});
  this->unanswered();  // minimum
  this->unanswered();  // maximum
  this->unanswered();  // step
  this->unanswered_factory_and_flags();

  this->canbus_.clear();
  for (unsigned chunk = 0; chunk < 300; chunk++)
    this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, static_cast<uint8_t>(chunk), 'a', 'b', 'c', 'd'});

  unsigned string_requests = 0;
  for (const auto &frame : this->canbus_.sent) {
    if ((frame.can_id >> MESSAGE_TYPE_SHIFT) == STRING_REQUEST_TYPE && frame.data[0] == STRING_REQUEST_MARKER)
      string_requests++;
  }
  EXPECT_EQ(string_requests, 8u);
  EXPECT_STREQ(this->hub_->get_scanned_field().name, "abcdabcdabcdabcdabcdabcdabcdabc");
}

// A dropdown answers with an index; what each option is called is a string the device holds, and
// selector 0x26 with the option's index says which. There are as many as the field's maximum.
TEST_F(MasterbusTest, ScanReadsTheNameOfEveryOptionOfAList) {
  scan_lines().clear();
  this->walk_to_list_field(MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_DROPDOWN, 2.0f);
  ASSERT_EQ(scan_lines().lines().size(), 3u);
  EXPECT_EQ(scan_lines().lines()[2], "field device=0x6C1001 tab=0 group=0 param=12 display=3 max=2");

  this->hub_->scan_step(this->scan_now_);
  const auto &asked = this->canbus_.sent.back();
  EXPECT_EQ(asked.can_id >> MESSAGE_TYPE_SHIFT, PROPERTY_REQUEST_TYPE);
  ASSERT_EQ(asked.can_data_length_code, 4);
  EXPECT_EQ(asked.data[0], static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_OPTION_STRING));
  EXPECT_EQ(asked.data[3], 0);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false, {0x26, 12, 0x00, 0x00, 0x40, 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x40, 0x00, 0x00, 'O', 'f', 'f', 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x26, 12, 0x00, 0x01, 0x41, 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x41, 0x00, 0x00, 'O', 'n'});
  // Two options, and the walk does not ask for a third.
  this->hub_->scan_step(this->scan_now_);
  EXPECT_NE(this->canbus_.sent.back().data[0],
            static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_OPTION_STRING));

  ASSERT_EQ(scan_lines().lines().size(), 5u);
  EXPECT_EQ(scan_lines().lines()[3], "option device=0x6C1001 tab=0 param=12 index=0 name=\"Off\"");
  EXPECT_EQ(scan_lines().lines()[4], "option device=0x6C1001 tab=0 param=12 index=1 name=\"On\"");
}

// An answer to a list property echoes the index it answers. One for the previous option arriving
// late is not this option's name.
TEST_F(MasterbusTest, AnOptionAnswerForAnotherIndexIsNotTaken) {
  scan_lines().clear();
  this->walk_to_list_field(MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_RADIO, 3.0f);
  this->answer(PROPERTY_INFORMATION_TYPE, {0x26, 12, 0x00, 0x00, 0x40, 0x00});
  this->answer(STRING_INFORMATION_TYPE, {0x30, 0x40, 0x00, 0x00, 'A', 0x00});
  // Asked for option 1; the answer for option 0 comes again.
  this->answer(PROPERTY_INFORMATION_TYPE, {0x26, 12, 0x00, 0x00, 0x40, 0x00});
  EXPECT_EQ(this->canbus_.sent.back().data[3], 1) << "the walk moved on on an answer for another index";
}

// A device list holds device addresses under selector 0x20, packed the way an announcement packs
// one; the maximum says how many. The entries below are the ones the battery blocks' event settings
// answered on the reference bus: BAT 1 and the display panel.
TEST_F(MasterbusTest, ScanReadsTheEntriesOfADeviceList) {
  scan_lines().clear();
  this->walk_to_list_field(MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_DEVICE_LIST, 2.0f);
  this->hub_->scan_step(this->scan_now_);
  EXPECT_EQ(this->canbus_.sent.back().data[0], static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_LIST_DEVICE));
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x20, 12, 0x00, 0x00, 0x1B, 0x01, 0x10, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x20, 12, 0x00, 0x01, 0x14, 0x01, 0x10, 0x00});

  ASSERT_EQ(scan_lines().lines().size(), 5u);
  EXPECT_EQ(scan_lines().lines()[3], "option device=0x6C1001 tab=0 param=12 index=0 entry=0x6C1001");
  EXPECT_EQ(scan_lines().lines()[4], "option device=0x6C1001 tab=0 param=12 index=1 entry=0x501001");
}

// An event field lists what another device can be told to do: selector 0x20 with index 0 names the
// device, packed as in an announcement, and 0x0F with each index one of its eventables by number.
// That device's count 0x40 + n says which of its fields eventable n is.
TEST_F(MasterbusTest, ScanReadsWhatAnEventFieldCommands) {
  scan_lines().clear();
  this->walk_to_list_field(MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_EVENT, 2.0f);
  this->answer(PROPERTY_INFORMATION_TYPE, {0x20, 12, 0x00, 0x00, 0x14, 0x01, 0x10, 0x00});
  this->hub_->scan_step(this->scan_now_);
  EXPECT_EQ(this->canbus_.sent.back().data[0], static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_EVENT_FIELD));
  EXPECT_EQ(this->canbus_.sent.back().data[3], 0);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false, {0x0F, 12, 0x00, 0x00, 0x00, 0x00});
  this->answer(PROPERTY_INFORMATION_TYPE, {0x0F, 12, 0x00, 0x01, 0x01, 0x00});

  ASSERT_EQ(scan_lines().lines().size(), 5u);
  EXPECT_EQ(scan_lines().lines()[3], "option device=0x6C1001 tab=0 param=12 index=0 target=0x501001 eventable=0");
  EXPECT_EQ(scan_lines().lines()[4], "option device=0x6C1001 tab=0 param=12 index=1 target=0x501001 eventable=1");
}

// Some equipment carries a longer form of a field's name. It is reported where it says more than
// the name does; a device that carries none is not asked again after three fields without one.
TEST_F(MasterbusTest, ScanReportsALongNameAndStopsAskingADeviceThatHasNone) {
  scan_lines().clear();
  this->feed("ext 0x046C1001 [8] 1B:01:10:00:51:00:00:02");
  this->hub_->report_scan();
  this->answer_eventables(2);
  this->answer_group_count(1);
  this->answer(GROUP_INFORMATION_TYPE, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0x40});  // five fields
  this->answer(GROUP_INFORMATION_TYPE, {0x28, 0x00, 0x00, 0x00, 0x00, 0x00});
  for (uint8_t param = 1; param <= 5; param++) {
    this->answer(GROUP_INFORMATION_TYPE, {0x03, 0x00, 0x00, static_cast<uint8_t>(param - 1), param, 0x00});
    this->answer(PROPERTY_INFORMATION_TYPE, {0x02, param, 0x00, 0x00, 0x01, 0x00});
    this->answer(PROPERTY_INFORMATION_TYPE, {0x28, param, 0x00, 0x00, 0x61, 0x00});  // name string 97
    for (int i = 0; i < 7; i++)
      this->unanswered();  // unit, minimum, maximum, step, factory, writeable, eventable
    this->hub_->scan_step(this->scan_now_);
    const bool asked_long = this->canbus_.sent.back().data[0] ==
                            static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_LONG_NAME_STRING);
    if (param == 1) {
      ASSERT_TRUE(asked_long);
      this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                           {0x29, param, 0x00, 0x00, 0x62, 0x00});  // long name string 98
      this->unanswered();                                           // no very long name
      this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 0x00});
      this->answer(STRING_INFORMATION_TYPE, {0x30, 0x62, 0x00, 0x00, 'B', 'a', 't', 't'});
      this->answer(STRING_INFORMATION_TYPE, {0x30, 0x62, 0x00, 0x01, 'e', 'r', 'y', 0x00});
      continue;
    }
    if (param <= 4) {
      // Asked of the next three fields too, which answer nothing.
      ASSERT_TRUE(asked_long) << "field " << unsigned{param};
      this->scan_now_ += SCAN_ANSWER_TIMEOUT_MS;
      this->hub_->scan_step(this->scan_now_);
    } else {
      EXPECT_FALSE(asked_long) << "a fourth field was asked for a long name after three without";
      this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                           {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 0x00});
      break;
    }
    this->answer(STRING_INFORMATION_TYPE, {0x30, 0x61, 0x00, 0x00, 'B', 'a', 't', 0x00});
  }

  ASSERT_GE(scan_lines().lines().size(), 3u);
  EXPECT_EQ(scan_lines().lines()[2],
            "field device=0x6C1001 tab=0 group=0 param=1 display=1 name=\"Bat\" long_name=\"Battery\"");
  EXPECT_EQ(scan_lines().lines()[3], "field device=0x6C1001 tab=0 group=0 param=2 display=1 name=\"Bat\"");
}

// What a device says about itself is read once it is online: a GeneralString question for which
// entry holds the string, then the string. The serial number here is the vendor's own made-up one.
TEST_F(MasterbusTest, ADevicesSerialNumberIsReadOnceItIsOnline) {
  auto *serial = this->add_info(MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_SERIAL_NUMBER);
  EXPECT_FALSE(this->hub_->info_step(this->info_now_)) << "a device nobody has heard from was asked";

  this->announce();
  this->hub_->info_step(this->info_now_);
  ASSERT_FALSE(this->canbus_.sent.empty());
  const auto &asked = this->canbus_.sent.back();
  EXPECT_EQ(asked.can_id >> MESSAGE_TYPE_SHIFT, STRING_REQUEST_TYPE);
  ASSERT_EQ(asked.can_data_length_code, 2);
  EXPECT_EQ(asked.data[0], GENERAL_STRING_SELECTOR);
  EXPECT_EQ(asked.data[1], GENERAL_STRING_SERIAL_NUMBER);
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {GENERAL_STRING_SELECTOR, GENERAL_STRING_SERIAL_NUMBER, 0x00, 0x00});
  this->info_answer(STRING_INFORMATION_TYPE, {0x30, 0x00, 0x00, 0x00, 'A', 'B', '0', '0'});
  this->info_answer(STRING_INFORMATION_TYPE, {0x30, 0x00, 0x00, 0x01, 'A', '0', '0', '0'});
  this->info_answer(STRING_INFORMATION_TYPE, {0x30, 0x00, 0x00, 0x02, '1'});

  ASSERT_EQ(serial->texts.size(), 1u);
  EXPECT_EQ(serial->texts[0], "AB00A0001");
  EXPECT_FALSE(this->hub_->info_step(this->info_now_)) << "the reader kept asking after it was done";
}

// And in the reading of what a device says about itself.
TEST_F(MasterbusTest, AnotherNodesLaterChunkDoesNotEndAnInformationRead) {
  auto *serial = this->add_info(MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_SERIAL_NUMBER);
  this->announce();
  this->info_answer(STRING_INFORMATION_TYPE, {GENERAL_STRING_SELECTOR, GENERAL_STRING_SERIAL_NUMBER, 0x05, 0x00});
  this->hub_->info_step(this->info_now_);  // asks for chunk 0 of string 5
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false, {0x30, 0x05, 0x00, 0x02, '1'});
  this->hub_->on_frame(frame_id(STRING_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x30, 0x05, 0x00, 0x00, 'A', 'B', '0', '0'});
  this->info_answer(STRING_INFORMATION_TYPE, {0x30, 0x05, 0x00, 0x01, 'A', '0', '0', '0'});
  this->info_answer(STRING_INFORMATION_TYPE, {0x30, 0x05, 0x00, 0x02, '1'});

  EXPECT_EQ(serial->unavailable_count, 0);
  ASSERT_EQ(serial->texts.size(), 1u);
  EXPECT_EQ(serial->texts[0], "AB00A0001");
}

TEST_F(MasterbusTest, AnInformationStringThatNeverEndsIsPublishedCutShortOnce) {
  auto *serial = this->add_info(MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_SERIAL_NUMBER);
  this->announce();
  this->info_answer(STRING_INFORMATION_TYPE, {GENERAL_STRING_SELECTOR, GENERAL_STRING_SERIAL_NUMBER, 0x05, 0x00});
  for (unsigned chunk = 0; chunk < 300; chunk++)
    this->info_answer(STRING_INFORMATION_TYPE, {0x30, 0x05, 0x00, static_cast<uint8_t>(chunk), '1', '2', '3', '4'});

  // The question for the string's id, then eight chunks.
  EXPECT_EQ(this->canbus_.sent.size(), 9u);
  ASSERT_EQ(serial->texts.size(), 1u);
  EXPECT_EQ(serial->texts[0], "1234123412341234123412341234123");
}

// The firmware is each processor's software version, asked from 0 until the device refuses one.
TEST_F(MasterbusTest, ADevicesFirmwareListsEveryProcessor) {
  auto *firmware = this->add_info(MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_FIRMWARE);
  this->announce();
  this->info_answer(STRING_INFORMATION_TYPE, {SOFTWARE_VERSION_SELECTOR, 0x00, 0x01, 0x3D, 0x00, 0x00});
  this->info_answer(STRING_INFORMATION_TYPE, {SOFTWARE_VERSION_SELECTOR, 0x01, 0x02, 0x07, 0x00, 0x00});
  this->info_answer(STRING_NOT_AVAILABLE_TYPE, {SOFTWARE_VERSION_SELECTOR, 0x02});

  ASSERT_EQ(firmware->texts.size(), 1u);
  EXPECT_EQ(firmware->texts[0], "61.1 7.2");
}

// A device that does not carry a string refuses the question, and one that ignores it is given up
// on after a second. Either way the entity is unavailable rather than showing something stale.
TEST_F(MasterbusTest, AStringTheDeviceDoesNotCarryIsUnavailable) {
  auto *revision = this->add_info(MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_REVISION_CODE);
  auto *name = this->add_info(MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_PRODUCT_NAME);
  this->announce();
  // The product name is asked first, in the vendor's numbering, and goes unanswered.
  this->hub_->info_step(this->info_now_);
  EXPECT_EQ(this->canbus_.sent.back().data[1], GENERAL_STRING_PRODUCT_NAME);
  this->info_now_ += 1000;
  this->info_answer(STRING_NOT_AVAILABLE_TYPE, {GENERAL_STRING_SELECTOR, GENERAL_STRING_REVISION_CODE});

  EXPECT_EQ(name->unavailable_count, 1);
  EXPECT_EQ(revision->unavailable_count, 1);
  EXPECT_TRUE(revision->texts.empty());
}

// The vendor's own tool reads a device again when its status version moves; the device asks for
// it with bit 5 of byte 6. The same announcement again asks for nothing.
TEST_F(MasterbusTest, ADeviceIsReadAgainWhenItsStatusVersionChangesOrItAsksToBe) {
  auto *article = this->add_info(MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_ARTICLE_NUMBER);
  const auto read_article = [this]() {
    this->info_answer(STRING_INFORMATION_TYPE, {GENERAL_STRING_SELECTOR, GENERAL_STRING_ARTICLE_NUMBER, 0x02, 0x00});
    this->info_answer(STRING_INFORMATION_TYPE, {0x30, 0x02, 0x00, 0x00, '6', '6', '0', 0x00});
  };
  this->announce(0x00, 0x02);
  read_article();
  ASSERT_EQ(article->texts.size(), 1u);

  this->announce(0x00, 0x02);
  EXPECT_FALSE(this->hub_->info_step(this->info_now_)) << "an unchanged announcement asked for a read";

  this->announce(0x00, 0x04);
  read_article();
  EXPECT_EQ(article->texts.size(), 2u);

  this->announce(ANNOUNCEMENT_READ_AGAIN_BIT, 0x04);
  read_article();
  EXPECT_EQ(article->texts.size(), 3u);
}

// Some equipment flips its status version on nearly every announcement - an interface in a 2022
// capture sent 08 and 00 by turns. Read again on each flip, a device whose announcements a hub asks
// for every few seconds would be read in full every few seconds. A flip within the device's timeout
// of the last read waits, and is read on the first announcement after it.
TEST_F(MasterbusTest, AStatusVersionThatFlipsBackAndForthIsReadOncePerTimeout) {
  this->battery_->set_timeout(60000);
  auto *article = this->add_info(MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_ARTICLE_NUMBER);
  const auto read_article = [this]() {
    this->info_answer(STRING_INFORMATION_TYPE, {GENERAL_STRING_SELECTOR, GENERAL_STRING_ARTICLE_NUMBER, 0x02, 0x00});
    this->info_answer(STRING_INFORMATION_TYPE, {0x30, 0x02, 0x00, 0x00, '6', '6', '0', 0x00});
  };
  uint32_t now = 1000;
  {
    LoopBlockingGuard clock{nullptr, nullptr, now};
    this->announce(0x00, 0x08);
  }
  read_article();
  for (int i = 0; i < 6; i++) {
    now += 9000;
    LoopBlockingGuard clock{nullptr, nullptr, now};
    this->announce(0x00, (i % 2) == 0 ? 0x00 : 0x08);
    read_article();
  }
  EXPECT_EQ(article->texts.size(), 1u);

  // Past the timeout, the flip that was owed is read, whatever this announcement says.
  now += 10000;
  {
    LoopBlockingGuard clock{nullptr, nullptr, now};
    this->announce(0x00, 0x08);
  }
  read_article();
  EXPECT_EQ(article->texts.size(), 2u);
}

// Lets a test stand in for the scheduler, which a host test does not run.
class SchedulingEntity : public RecordingEntity {
 public:
  using RecordingEntity::RecordingEntity;
  using MasterbusEntity::read_back_;
  using MasterbusEntity::questions_unanswered_;
};

static bool is_data_write(const canbus::CanFrame &frame) {
  return (frame.can_id >> MESSAGE_TYPE_SHIFT) == MONITORING_REQUEST_TYPE &&
         frame.can_data_length_code == MONITORING_WRITE_LENGTH;
}

// The first write to a field asks the device whether the field takes one and whether it is an
// event, and goes out once it has said - for an event, followed by who wrote it.
TEST_F(MasterbusTest, AFieldIsWrittenOnceTheDeviceSaysItTakesAWrite) {
  SchedulingEntity entity(this->battery_.get(), 117, MasterbusTab::MASTERBUS_TAB_MONITORING,
                          MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);
  this->hub_->register_entity(&entity);

  EXPECT_TRUE(entity.write(1.0f));
  ASSERT_EQ(this->canbus_.sent.size(), 2u);
  for (const auto &asked : this->canbus_.sent) {
    EXPECT_EQ(asked.can_id >> MESSAGE_TYPE_SHIFT, PROPERTY_REQUEST_TYPE);
    ASSERT_EQ(asked.can_data_length_code, 3);
    EXPECT_EQ(asked.data[1], 117);
  }
  EXPECT_EQ(this->canbus_.sent[0].data[0], static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_WRITEABLE));
  EXPECT_EQ(this->canbus_.sent[1].data[0], static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_EVENTABLE));

  // A yes alone is not enough: whether the next field is touched depends on the second answer.
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x0B, 117, 0x00, 0x00, 0x01, 0x00});
  ASSERT_EQ(this->canbus_.sent.size(), 2u);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x0D, 117, 0x00, 0x00, 0x01, 0x00});
  ASSERT_EQ(this->canbus_.sent.size(), 4u);
  EXPECT_TRUE(is_data_write(this->canbus_.sent[2]));
  EXPECT_EQ(this->canbus_.sent[2].data[0], 117);
  EXPECT_TRUE(is_data_write(this->canbus_.sent[3]));
  EXPECT_EQ(this->canbus_.sent[3].data[0], 118);

  // The answers are kept: the next write goes out at once.
  this->canbus_.clear();
  EXPECT_TRUE(entity.write(1.0f));
  ASSERT_EQ(this->canbus_.sent.size(), 2u);
  EXPECT_TRUE(is_data_write(this->canbus_.sent[0]));
}

// A field that says it takes no write is not written, and is not asked again.
TEST_F(MasterbusTest, AFieldThatSaysItTakesNoWriteIsNotWritten) {
  SchedulingEntity entity(this->battery_.get(), 1, MasterbusTab::MASTERBUS_TAB_MONITORING,
                          MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT);
  this->hub_->register_entity(&entity);
  entity.write(5.0f);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false, {0x0B, 1, 0x00, 0x00, 0x00, 0x00});
  this->canbus_.clear();

  EXPECT_FALSE(entity.write(5.0f));
  EXPECT_TRUE(this->canbus_.sent.empty());
}

// A write the device refuses is refused before it has said whether the field is an event.
TEST_F(MasterbusTest, ANoNeedsNoSecondAnswer) {
  SchedulingEntity entity(this->battery_.get(), 1, MasterbusTab::MASTERBUS_TAB_MONITORING,
                          MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT);
  this->hub_->register_entity(&entity);
  entity.write(5.0f);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false, {0x0B, 1, 0x00, 0x00, 0x00, 0x00});
  this->canbus_.clear();
  entity.questions_unanswered_();
  EXPECT_TRUE(this->canbus_.sent.empty());
}

// A display panel's settings answer that they take a write and are no event: the write is the
// value alone, and the field after it is not touched.
TEST_F(MasterbusTest, ASettingIsWrittenWithoutTouchingTheNextField) {
  SchedulingEntity entity(this->battery_.get(), 42, MasterbusTab::MASTERBUS_TAB_MONITORING,
                          MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT);
  this->hub_->register_entity(&entity);
  entity.write(70.0f);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false, {0x0B, 42, 0x00, 0x00, 0x01, 0x00});
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false, {0x0D, 42, 0x00, 0x00, 0x00, 0x00});

  ASSERT_EQ(this->canbus_.sent.size(), 3u);
  EXPECT_TRUE(is_data_write(this->canbus_.sent[2]));
  EXPECT_EQ(this->canbus_.sent[2].data[0], 42);
}

// A device that says a field takes a write but not whether it is an event has it written as no
// event: the frame after it would land on a field nobody asked to change.
TEST_F(MasterbusTest, AFieldThatDoesNotSayItIsAnEventIsWrittenAsNone) {
  SchedulingEntity entity(this->battery_.get(), 42, MasterbusTab::MASTERBUS_TAB_MONITORING,
                          MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT);
  this->hub_->register_entity(&entity);
  entity.write(70.0f);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false, {0x0B, 42, 0x00, 0x00, 0x01, 0x00});
  entity.questions_unanswered_();

  ASSERT_EQ(this->canbus_.sent.size(), 3u);
  EXPECT_TRUE(is_data_write(this->canbus_.sent[2]));
  EXPECT_EQ(this->canbus_.sent[2].data[0], 42);
}

// The answers are broadcast like any other, so ones a display panel asked for count too.
TEST_F(MasterbusTest, SomebodyElsesAnswerOnWhetherAFieldTakesAWriteCounts) {
  SchedulingEntity entity(this->battery_.get(), 117, MasterbusTab::MASTERBUS_TAB_MONITORING,
                          MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);
  this->hub_->register_entity(&entity);
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x0B, 117, 0x00, 0x00, 0x01, 0x00});
  this->hub_->on_frame(frame_id(PROPERTY_INFORMATION_TYPE, BATTERY_1), true, false,
                       {0x0D, 117, 0x00, 0x00, 0x01, 0x00});

  EXPECT_TRUE(entity.write(1.0f));
  ASSERT_EQ(this->canbus_.sent.size(), 2u);
  EXPECT_TRUE(is_data_write(this->canbus_.sent[0]));
}

// A device that does not answer the question is written anyway once it has had its chance, and
// the field is read back afterwards - our own read, so it does not stand in for the next poll.
TEST_F(MasterbusTest, AnUnansweredQuestionStillLetsTheWriteThroughAndTheFieldIsReadBack) {
  SchedulingEntity entity(this->battery_.get(), 20, MasterbusTab::MASTERBUS_TAB_MONITORING,
                          MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT);
  this->hub_->register_entity(&entity);
  entity.write(42.0f);
  ASSERT_EQ(this->canbus_.sent.size(), 2u);
  EXPECT_EQ(this->canbus_.sent.back().can_id >> MESSAGE_TYPE_SHIFT, PROPERTY_REQUEST_TYPE);
  entity.questions_unanswered_();
  ASSERT_EQ(this->canbus_.sent.size(), 3u);
  EXPECT_EQ(this->canbus_.sent[2].can_id >> MESSAGE_TYPE_SHIFT, MONITORING_REQUEST_TYPE);
  EXPECT_EQ(this->canbus_.sent[2].can_data_length_code, MONITORING_WRITE_LENGTH);

  entity.read_back_();
  ASSERT_EQ(this->canbus_.sent.size(), 4u);
  EXPECT_EQ(this->canbus_.sent.back().can_id >> MESSAGE_TYPE_SHIFT, MONITORING_REQUEST_TYPE);
  EXPECT_EQ(this->canbus_.sent.back().can_data_length_code, MONITORING_REQUEST_LENGTH);
}

// The configuration tab holds a device's settings, and the vendor's own tool always has the device's
// word on whether a field there takes a write before it writes one. A configuration field that does
// not say is not written.
TEST_F(MasterbusTest, AConfigurationFieldThatDoesNotSayItTakesAWriteIsNotWritten) {
  SchedulingEntity entity(this->battery_.get(), 20, MasterbusTab::MASTERBUS_TAB_CONFIGURATION,
                          MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT);
  this->hub_->register_entity(&entity);
  entity.write(42.0f);
  ASSERT_EQ(this->canbus_.sent.size(), 2u);
  EXPECT_EQ(this->canbus_.sent.back().can_id >> MESSAGE_TYPE_SHIFT, 0x38);
  entity.questions_unanswered_();
  EXPECT_EQ(this->canbus_.sent.size(), 2u);

  // It is asked again before the next write, and written once it says yes.
  this->canbus_.clear();
  entity.write(42.0f);
  ASSERT_EQ(this->canbus_.sent.size(), 2u);
  this->hub_->on_frame(frame_id(0x18, BATTERY_1), true, false, {0x0B, 20, 0x00, 0x00, 0x01, 0x00});
  entity.questions_unanswered_();
  ASSERT_EQ(this->canbus_.sent.size(), 3u);
  EXPECT_EQ(this->canbus_.sent[2].can_id >> MESSAGE_TYPE_SHIFT, 0x37);
  EXPECT_EQ(this->canbus_.sent[2].can_data_length_code, MONITORING_WRITE_LENGTH);
}

// A hub can speak under an address of its own, in the node request and after every write.
TEST_F(MasterbusTest, AHubWithAnAddressOfItsOwnSpeaksUnderIt) {
  this->hub_->set_address(0x3BFFFE);
  this->hub_->request_nodes();
  EXPECT_EQ(this->canbus_.sent.back().can_id, frame_id(NODE_REQUEST_TYPE, 0x3BFFFE));

  SchedulingEntity entity(this->battery_.get(), 117, MasterbusTab::MASTERBUS_TAB_MONITORING,
                          MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN);
  entity.on_eventable_answer(true);
  this->hub_->write_value(entity, 1.0f);
  const auto &after = this->canbus_.sent.back();
  EXPECT_EQ(encode_uint32(after.data[5], after.data[4], after.data[3], after.data[2]), 0x3BFFFEu);
}

}  // namespace esphome::masterbus::testing
