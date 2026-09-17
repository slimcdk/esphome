#pragma once

#include "esphome/core/defines.h"

#ifdef USE_MASTERBUS_SCAN

#include "masterbus_protocol.h"

#include <cinttypes>
#include <vector>

namespace esphome::masterbus {

class MasterbusHub;

/// The longest name and unit a scan will keep. Names seen on real equipment run to about fifteen
/// characters ("Bat. temperature"); anything longer is truncated rather than dropped.
static constexpr uint8_t SCAN_NAME_LENGTH = 32;
static constexpr uint8_t SCAN_UNIT_LENGTH = 12;

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
  float minimum;
  float maximum;
  float step;
  bool has_limits;
  char name[SCAN_NAME_LENGTH];
  char unit[SCAN_UNIT_LENGTH];
};

/** Walks the devices a scan discovered and reports their fields as pasteable configuration.
 *
 * A device answers one question at a time, so this is a state machine: each tick sends one
 * request and consumes the answer to the previous one. A question a device does not answer is
 * taken as "this does not exist" after a timeout, which is also how the end of a group list and
 * the end of a field list are found - the vendor's own group count is not decoded.
 */
class MasterbusScanner {
 public:
  explicit MasterbusScanner(MasterbusHub *hub) : hub_(hub) {}

  /// Begin walking the devices discovered so far. Does nothing if a walk is already running.
  void start();
  bool is_running() const { return this->phase_ != Phase::PHASE_IDLE; }

  /// Drive one step. Sends at most one request per call.
  void loop();

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
    PHASE_FIELD_NAME_TEXT,
    PHASE_FIELD_UNIT_TEXT,
  };

  void send_current_();
  void advance_(bool answered);
  void finish_field_();
  void next_tab_();
  void next_device_();
  void report_field_();

  MasterbusHub *hub_;
  Phase phase_{Phase::PHASE_IDLE};
  // A separate flag rather than a zero timestamp: the clock legitimately reads zero.
  bool waiting_{false};
  uint32_t sent_at_{0};

  uint8_t device_index_{0};
  /// Which tab is being walked. The four are the same protocol under different message numbers,
  /// so the walk below is unchanged by it - only the numbers the hub puts on the wire differ.
  MasterbusTab tab_{MasterbusTab::MASTERBUS_TAB_MONITORING};
  uint16_t group_{0};
  /// The name of the group being walked, printed once above its fields. Empty when the device
  /// does not name the group.
  char group_name_[SCAN_NAME_LENGTH]{};
  bool group_reported_{false};
  uint16_t field_index_{0};
  uint16_t fields_in_group_{0};
  /// The string being fetched, whether that is a group's name or a field's. Only one string is
  /// ever in flight, so one slot is enough.
  uint16_t name_string_{0};
  uint16_t unit_string_{0};
  uint8_t chunk_{0};
  uint16_t completed_{0};
  /// The platform key last printed, so it is not repeated for every consecutive field.
  const char *last_platform_{nullptr};
  MasterbusScannedField field_{};
};

}  // namespace esphome::masterbus

#endif
