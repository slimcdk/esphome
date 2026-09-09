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

TEST_F(MasterbusTest, PollSkipsTabsWithNoKnownRequestFormat) {
  auto *setting =
      add_sensor(1, MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT, MasterbusTab::MASTERBUS_TAB_CONFIGURATION);

  setting->update();

  EXPECT_TRUE(this->canbus_.sent.empty());
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
  // The whole message is the identifier: type 0x05 against an address that belongs to no device.
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

}  // namespace esphome::masterbus::testing
