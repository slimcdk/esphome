#pragma once

#include "esphome/core/defines.h"

#ifdef USE_MASTERBUS_SCAN

#include "masterbus_protocol.h"

#include <cinttypes>
#include <vector>

namespace esphome::masterbus {

class MasterbusHub;

/// How long a question waits before it is taken as unanswered, which is how the walk finds the
/// end of a list and which properties a field does not carry.
static constexpr uint32_t SCAN_ANSWER_TIMEOUT_MS = 200;
/// The longest name and unit a scan will keep. Names seen on real equipment run to about fifteen
/// characters ("Bat. temperature"); anything longer is truncated rather than dropped.
static constexpr uint8_t SCAN_NAME_LENGTH = 32;
static constexpr uint8_t SCAN_UNIT_LENGTH = 12;
/// How many processors a device's software versions are kept for. The vendor's example device has
/// one; past this many the walk stops asking.
static constexpr uint8_t SCAN_MAX_PROCESSORS = 8;

/** What the scan learned about one field.
 *
 * Held one field at a time and rendered as soon as it is complete, so walking a whole bus costs
 * no more memory than walking a single field.
 */
struct MasterbusScannedField {
  uint16_t param;
  uint16_t group;
  MasterbusTab tab;
  MasterbusDisplayType display_type;
  /// Not a number where the device did not answer for that property, which is ordinary: a
  /// boolean has no minimum.
  float minimum;
  float maximum;
  float step;
  /// The factory value, likewise not a number where the device did not answer for it.
  float factory;
  /// Whether the device says the field takes a write, and whether it is an event. -1 where the
  /// device did not answer, otherwise 0 or 1.
  int8_t writable;
  int8_t eventable;
  /// Whether the field carries a name or a unit string at all, whatever the device then sent for
  /// it. A field with a name string the device would not hand over reads as an empty name.
  bool has_name;
  bool has_unit;
  char name[SCAN_NAME_LENGTH];
  char unit[SCAN_UNIT_LENGTH];
  /// The longer forms of the name some equipment carries, empty where it carries none.
  char long_name[SCAN_NAME_LENGTH];
  char very_long_name[SCAN_NAME_LENGTH];
};

/** Walks the devices a scan discovered and reports their fields as pasteable configuration.
 *
 * A device answers one question at a time, so this is a state machine: each tick sends one
 * request and consumes the answer to the previous one. A question a device does not answer is
 * taken as "this does not exist" after a timeout, which is also how the end of a list is found.
 */
class MasterbusScanner {
 public:
  explicit MasterbusScanner(MasterbusHub *hub) : hub_(hub) {}

  /// Begin walking the devices discovered so far. Does nothing if a walk is already running.
  void start();
  bool is_running() const { return this->phase_ != Phase::PHASE_IDLE; }

  /// Drive the walk one step, at the time the caller read off the clock. Sends at most one
  /// request per call.
  void loop(uint32_t now);

  /// Offer a frame to the scan. Returns whether it answered what the scan was waiting for.
  bool on_frame(uint8_t type, uint32_t address, const std::vector<uint8_t> &data);

  /// The field most recently completed. Exposed so a test can assert on what was learned rather
  /// than on the wording of a log line.
  const MasterbusScannedField &get_last_field() const { return this->field_; }
  /// The name of the group being walked, for the same reason.
  const char *get_group_name() const { return this->group_name_; }

 protected:
  enum class Phase : uint8_t {
    PHASE_IDLE,
    PHASE_EVENTABLES,
    PHASE_DEVICE_STRING_ID,
    PHASE_DEVICE_STRING_TEXT,
    PHASE_SOFTWARE_VERSION,
    PHASE_GENERAL_COUNT,
    PHASE_GROUP_COUNT,
    PHASE_GROUP_FIELD_COUNT,
    PHASE_GROUP_NAME_ID,
    PHASE_GROUP_NAME_TEXT,
    PHASE_FIELD_NUMBER,
    PHASE_FIELD_DISPLAY_TYPE,
    PHASE_FIELD_NAME_ID,
    PHASE_FIELD_UNIT_ID,
    PHASE_FIELD_MINIMUM,
    PHASE_FIELD_MAXIMUM,
    PHASE_FIELD_STEP,
    PHASE_FIELD_FACTORY,
    PHASE_FIELD_WRITEABLE,
    PHASE_FIELD_EVENTABLE,
    PHASE_FIELD_LONG_NAME_ID,
    PHASE_FIELD_VERY_LONG_NAME_ID,
    PHASE_FIELD_NAME_TEXT,
    PHASE_FIELD_UNIT_TEXT,
    PHASE_FIELD_LONG_NAME_TEXT,
    PHASE_FIELD_VERY_LONG_NAME_TEXT,
    PHASE_OPTION_ID,
    PHASE_OPTION_TEXT,
    PHASE_LIST_DEVICE,
    PHASE_EVENT_SOURCE,
    PHASE_EVENT_FIELD,
  };

  void send_current_();
  void advance_(bool answered);
  void next_device_string_();
  void report_device_();
  void next_general_count_();
  /// Read the field's strings out of the table, one after the other, then report the field.
  void next_field_text_();
  /// Report the field and walk its list, if it has one.
  void complete_field_();
  void next_entry_();
  void next_field_();
  void next_group_();
  void next_tab_();
  void next_device_();
  void report_field_();
  void report_entry_(uint32_t value);
  /// Where the answers to the walk on the current tab arrive. Every tab answers in the same shape,
  /// so this number is all that keeps one tab's answer from being taken for another's.
  uint8_t group_answer_type_() const {
    return tab_message(this->tab_, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_GROUP).information;
  }
  uint8_t property_answer_type_() const {
    return tab_message(this->tab_, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_PROPERTY).information;
  }

  MasterbusHub *hub_;
  Phase phase_{Phase::PHASE_IDLE};
  // A separate flag rather than a zero timestamp: the clock legitimately reads zero.
  bool waiting_{false};
  uint32_t sent_at_{0};

  uint8_t device_index_{0};
  /// How many event slots this device has, and whether it answered the question at all. Equipment
  /// that does not carry it refuses outright, which is not an error.
  uint16_t eventables_{0};
  bool eventables_known_{false};
  /// The device's own strings - article number, serial number, product name and revision code, in
  /// the vendor's numbering from 1 - and which of them it handed over.
  uint8_t device_string_{0};
  char device_strings_[4][SCAN_NAME_LENGTH]{};
  uint8_t device_strings_known_{0};
  /// The software version of each processor that answered, and which one is being asked.
  uint8_t processor_{0};
  uint16_t software_versions_[SCAN_MAX_PROCESSORS]{};
  /// The count being asked, of the ones every device is asked once.
  uint8_t general_count_{0};
  /// How many fields in a row went without an answer for a long name. Most equipment carries none,
  /// and asking every field would cost a timeout each.
  uint8_t long_names_silent_{0};
  /// Which tab is being walked. The four are the same protocol under different message numbers,
  /// so the walk below is unchanged by it - only the numbers the hub puts on the wire differ.
  MasterbusTab tab_{MasterbusTab::MASTERBUS_TAB_MONITORING};
  uint16_t group_{0};
  /// How many groups this tab holds, and whether the device answered for it. A device that does
  /// not carry the question is walked the older way, until a group goes unanswered.
  uint16_t groups_in_tab_{0};
  bool groups_known_{false};
  /// Whether the string being assembled had to be cut to fit, and whether the field cap has
  /// already been reported for the group being walked.
  bool string_truncated_{false};
  bool cap_reported_{false};
  /// The name of the group being walked, printed once above its fields. Empty when the device
  /// does not name the group.
  char group_name_[SCAN_NAME_LENGTH]{};
  bool group_reported_{false};
  /// Whether the group being walked carries a name string.
  bool group_named_{false};
  uint16_t field_index_{0};
  uint16_t fields_in_group_{0};
  /// The string being fetched, whether that is a device's, a group's or a field's name. Only one
  /// string is ever in flight, so one slot is enough; a field's other strings wait their turn.
  uint16_t name_string_{0};
  uint16_t unit_string_{0};
  uint16_t long_name_string_{0};
  uint16_t very_long_name_string_{0};
  uint8_t chunk_{0};
  uint16_t completed_{0};
  /// The entry of a list field being asked for, and what it is known by so far: an option's
  /// string and text, and the device an event watches.
  uint8_t entry_{0};
  uint16_t option_string_{0};
  char option_text_[SCAN_NAME_LENGTH]{};
  uint32_t event_source_{0};
  bool event_source_known_{false};
  MasterbusScannedField field_{};
};

}  // namespace esphome::masterbus

#endif
