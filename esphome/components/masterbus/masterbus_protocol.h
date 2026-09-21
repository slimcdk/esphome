#pragma once

#include <algorithm>
#include <cinttypes>
#include <cstddef>

namespace esphome::masterbus {

// The MasterBus protocol, in two halves.
//
// The wire format below is not published and was derived by watching a live installation. Each
// constant carries how far it is actually known, so that confirming one is a change to this file
// alone:
//
//   VERIFIED    Measured against captured traffic, or read out of the vendor SDK's data files.
//   UNVERIFIED  Inferred and not yet contradicted. Treat as a guess.
//
// The data model further down is different in kind: it comes from the vendor's own documented
// header rather than from guesswork, so the scale above does not apply to it.

// ---------------------------------------------------------------------------
// Identifier layout
// ---------------------------------------------------------------------------

/// VERIFIED: the extended identifier splits into a six bit message type and a 23 bit device
/// address. Every address so decoded matched a device the vendor library independently reported by
/// the same number - eight of eight on this bus.
///
/// The split was settled by payload length. Read as six bits, each type carries a near-fixed
/// length: announcements are always eight bytes, node requests always zero, monitoring answers
/// always six, monitoring requests always two. Read as five bits, those same types show mixed
/// lengths, because a five bit read merges two neighbouring types into one.
static constexpr uint32_t DEVICE_ADDRESS_MASK = 0x007FFFFF;
static constexpr uint8_t MESSAGE_TYPE_SHIFT = 23;

// ---------------------------------------------------------------------------
// Monitoring messages
// ---------------------------------------------------------------------------

/// VERIFIED: a monitoring value is requested with the field number and answered with the field
/// number and a little-endian 32 bit float. 39553 request/response pairs matched in capture
/// against 86 that did not, with a median turnaround of 0.71 ms.
///
///     request   type 0x30, 2 bytes: field number little-endian
///     response  type 0x10, 6 bytes: field number little-endian, then float32 little-endian
///
/// The type is the value *before* the shift, which is easy to confuse with the identifier that
/// ends up on the wire. Asking device 0x6D56EA for field 117 is (0x30 << 23) | 0x6D56EA, which
/// reads as 186D56EA#7500 in a candump log - not 306D56EA.
static constexpr uint8_t MONITORING_REQUEST_TYPE = 0x30;
static constexpr uint8_t MONITORING_INFORMATION_TYPE = 0x10;
static constexpr uint8_t MONITORING_REQUEST_LENGTH = 2;
static constexpr uint8_t MONITORING_INFORMATION_LENGTH = 6;
// UNVERIFIED: the history and configuration tabs are read with their own message numbers but are
// assumed to use the two lengths above, because every other message they share with monitoring -
// group and property - is byte for byte the same across tabs. An answer of a different length is
// dropped rather than misread, so a wrong assumption shows up as silence, not as a wrong value.

/// VERIFIED: a monitoring field is written with the same message type that reads it. What tells
/// the two apart is the payload length: two bytes asks, six bytes writes.
///
///     write     type 0x30, 6 bytes: field number little-endian, then float32 little-endian
///
/// Captured while driving the vendor library's own "Open relay" and "Close relay" calls against a
/// battery block: twelve writes, all of this shape, and the relay opened on the first of them.
/// Only the value 1.0 has been seen on the wire, because the fields exercised were event fields
/// that carry no other value. Writing anything else is therefore not confirmed.
static constexpr uint8_t MONITORING_WRITE_LENGTH = 6;

/// UNVERIFIED as to meaning, VERIFIED as to necessity: every write the vendor library sent was
/// immediately followed by a second write, to the field one higher, carrying these four bytes.
/// The value never varied - the same bytes followed a write to field 117 and to field 119, so it
/// is not derived from the field or the value. Neither of the higher fields exists in the device's
/// own field list, so this is not an ordinary write and reads more like a commit. It is sent
/// because the library sends it, not because its meaning is understood.
static constexpr uint8_t MONITORING_WRITE_COMMIT_LENGTH = 4;
inline const uint8_t *monitoring_write_commit() {
  static const uint8_t COMMIT[MONITORING_WRITE_COMMIT_LENGTH] = {0x01, 0x00, 0x50, 0x00};
  return COMMIT;
}

/// VERIFIED, and the reason writing is not fire-and-forget: a write that goes out correctly is not
/// the same as a command that took effect. Equipment can be shared between devices, and then only
/// one of them accepts the command for it. On the reference installation two battery blocks share
/// one contactor: either block will open it, but only one of the two closes it again. Fourteen
/// correctly formed close writes to the other block changed nothing, while the same frame sent to
/// its neighbour worked on the first attempt, ten seconds after an open.
///
/// So a caller that needs the result must read the field back afterwards. This component neither
/// retries nor guesses which device to address: which device owns what is a property of the
/// installation rather than of the protocol, and belongs in the user's configuration.

// VERIFIED: a device answers a monitoring field only when asked. The same capture holds 42152
// answers that followed a request against 20 that did not, so a hub that never transmits reports
// values only for as long as something else on the bus is polling the same fields. This is why
// poll_interval exists.

// ---------------------------------------------------------------------------
// Dates and times
// ---------------------------------------------------------------------------

/// VERIFIED: a date and a time arrive as the ordinary monitoring float. A time counts seconds. A
/// date packs the calendar into one number:
///
///     year * 416 + month * 32 + day
///
/// which is to say months of 32 days and years of 13 months, so that the parts come back out with
/// a division rather than with a calendar. The spare slots - a 32nd day, a 13th month - are what
/// make it reversible.
///
/// Checked against a capture from an unrelated installation, a Mastervolt DC shunt in January
/// 2022. Its date field read 841202, which unpacks to day 18, month 1, year 2022, and the capture
/// was recorded on 2022-01-18. Its time field advanced 84 counts over 83.88 seconds of that same
/// capture, so the unit is the second.
///
/// A time is not always a clock. The same display type serves the shunt's "Time", which is a time
/// of day, and its "Remaining", which is a span of hours. So the hour is not folded into a day:
/// both render as h:mm:ss, and which one a field means is the field's business.
static constexpr uint32_t DATE_DAYS_PER_MONTH = 32;
static constexpr uint32_t DATE_MONTHS_PER_YEAR = 13;
static constexpr uint32_t SECONDS_PER_HOUR = 60 * 60;

struct MasterbusDate {
  uint16_t year;
  uint8_t month;
  uint8_t day;
};

struct MasterbusTime {
  uint16_t hour;
  uint8_t minute;
  uint8_t second;
};

/// Unpack a date field. Returns false, and leaves `out` alone, for a number that cannot be one -
/// which also catches the NaN a device that has no answer reports.
inline bool decode_date(float value, MasterbusDate &out) {
  // The year has to survive the cast as well as the packing, so the bound is the largest year the
  // struct can hold rather than the largest float.
  if (!(value >= 0.0f) || value >= static_cast<float>(DATE_MONTHS_PER_YEAR * DATE_DAYS_PER_MONTH) * 65536.0f)
    return false;
  const uint32_t packed = static_cast<uint32_t>(value);
  const uint32_t months = packed / DATE_DAYS_PER_MONTH;
  out.year = static_cast<uint16_t>(months / DATE_MONTHS_PER_YEAR);
  out.month = static_cast<uint8_t>(months % DATE_MONTHS_PER_YEAR);
  out.day = static_cast<uint8_t>(packed % DATE_DAYS_PER_MONTH);
  return true;
}

/// Unpack a time field, on the same terms as decode_date.
inline bool decode_time(float value, MasterbusTime &out) {
  if (!(value >= 0.0f) || value >= static_cast<float>(SECONDS_PER_HOUR) * 65536.0f)
    return false;
  const uint32_t seconds = static_cast<uint32_t>(value);
  out.hour = static_cast<uint16_t>(seconds / SECONDS_PER_HOUR);
  out.minute = static_cast<uint8_t>((seconds / 60) % 60);
  out.second = static_cast<uint8_t>(seconds % 60);
  return true;
}

// ---------------------------------------------------------------------------
// Device announcements
// ---------------------------------------------------------------------------

/// VERIFIED: asking every device to announce itself is a frame with no payload at all. The whole
/// message is the identifier - the node request type against the address of whoever is asking.
/// Every device on the bus answers it except the one whose address it carries: 120 requests
/// carrying a real device's address were answered by exactly the seven others. So the address
/// below is not a broadcast address but the vendor library's own node identity, which this hub
/// borrows. Captured while the vendor library opened a fresh context: it sends the request three
/// times in a row, and every device answers within about four milliseconds.
///
/// Measured over 1797 requests in a 10 minute capture: exactly eight distinct devices answered
/// each one - minimum eight, maximum eight - with a median delay of 5.8 ms.
static constexpr uint8_t NODE_REQUEST_TYPE = 0x0A;
/// DERIVED: the vendor names a NodeNa beside NodeInfo and NodeReq, and the string family refuses
/// one above its answer. No refusal to a node request has been captured - every device on the
/// reference bus answers every time - so this is the pattern applied, not a measurement.
static constexpr uint8_t NODE_NOT_AVAILABLE_TYPE = 0x09;
static constexpr uint32_t NODE_REQUEST_ADDRESS = 0x500001;
/// The library repeats it, so a device that missed the first still answers.
static constexpr uint8_t NODE_REQUEST_REPEATS = 3;

/// VERIFIED: a device answers with its own address in the first four payload bytes, in an odd
/// packing: the high bits in the first byte, then the low sixteen little-endian, then the two
/// remaining bits in the low end of the fourth byte. All eight addresses in a 15216 frame sample
/// rebuilt exactly, and they were precisely the eight devices the vendor library reports for this
/// bus.
///
/// The first byte has room for six bits but the address only has five left to give: sixteen plus
/// two plus five is the 23 bits the identifier carries. Every announcement seen leaves that sixth
/// bit clear, so what it means is unknown - it is masked off rather than shifted into an address
/// no configured device could ever match.
///
/// VERIFIED: the remaining four bytes are where a device's own state lives, and there is no
/// message that carries it. The vendor library reports a device's status without putting a single
/// frame on the bus - two calls, 48 ms each, nothing addressed to that device in either window -
/// so it can only be reading the announcements its own polling keeps bringing in. Do not go
/// looking for a status request; decode these bytes instead.
///
/// VERIFIED, and it matters to anyone who wants those bytes: an announcement is an answer, never
/// sent unprompted. A 10 minute capture held exactly as many announcements as the node requests in
/// it called for - 1797 requests answered by eight devices and 120 by seven, 15216 in all, not one
/// left over. "About three a second" was the vendor bridge's own polling rate. On a bus where this
/// hub is the only node, nothing announces itself until the hub asks.
///
/// Which byte holds the state is still open. Six identical battery blocks, one article number,
/// send three different patterns in bytes 5-7 (0x00 0x00 0x02, 0x00 0x00 0x04 and 0x03 0x00 0x04),
/// so those bytes do not simply name the product either. The one block that answers the cluster
/// fields is also the only one with 0x02 in byte 7. Byte 4 moves between captures with no state
/// change. Settling it needs a device in a different state, not more traffic.
static constexpr uint8_t DEVICE_ANNOUNCEMENT_TYPE = 0x08;
static constexpr uint8_t DEVICE_ANNOUNCEMENT_LENGTH = 8;

inline uint32_t decode_announced_address(const uint8_t *data) {
  const uint32_t address = (static_cast<uint32_t>(data[0]) << 18) | (static_cast<uint32_t>(data[3] & 0x03) << 16) |
                           (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[1]);
  return address & DEVICE_ADDRESS_MASK;
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

/// VERIFIED: a device holds its text in a numbered string table, read four bytes at a time.
///
///     request   type 0x0E, 4 bytes: 0x30, string id little-endian, chunk index
///     answer    type 0x0C, the 4 byte header then up to four bytes of ASCII
///     absent    type 0x0D, the 4 byte header alone
///
/// Chunks are concatenated and terminated by a NUL. Checked by reassembling strings from a capture
/// and comparing them with what the vendor library reported for the same device: a serial number
/// came back in three chunks and reassembled exactly, and field names and units matched too.
static constexpr uint8_t STRING_REQUEST_TYPE = 0x0E;
static constexpr uint8_t STRING_INFORMATION_TYPE = 0x0C;
static constexpr uint8_t STRING_NOT_AVAILABLE_TYPE = 0x0D;
static constexpr uint8_t STRING_REQUEST_MARKER = 0x30;
static constexpr uint8_t STRING_CHUNK_LENGTH = 4;

/// Copy one chunk of a string answer into place and say whether the string ended there. Where the
/// text belongs is the chunk number the answer carries rather than a counter of our own: believing
/// our own once put the second half of "Battery" over the first.
///
/// `data` is a whole string answer, header included. `out` is always left NUL terminated.
inline bool take_string_chunk(const uint8_t *data, size_t size, char *out, uint8_t capacity) {
  // A short answer is the last one: the device sends four bytes of text until it runs out.
  bool ended = size < STRING_CHUNK_LENGTH + 4;
  size_t at = static_cast<size_t>(data[3]) * 4;
  for (size_t i = STRING_CHUNK_LENGTH; i < size; i++) {
    if (data[i] == 0) {
      ended = true;
      break;
    }
    if (at < static_cast<size_t>(capacity) - 1)
      out[at++] = static_cast<char>(data[i]);
  }
  out[std::min<size_t>(at, capacity - 1)] = '\0';
  return ended;
}

// ---------------------------------------------------------------------------
// Field properties
// ---------------------------------------------------------------------------

/// VERIFIED: a field's metadata is read one property at a time.
///
///     request   type 0x31, 3 bytes: property id, field number little-endian
///     answer    type 0x11, the 3 byte header, one tag byte, then the value
///
/// The value is a float32 for the numeric properties and a little-endian 16 bit number for the
/// rest. A property a field does not have simply goes unanswered - a boolean field returns nothing
/// for minimum, maximum, step or unit.
///
/// Confirmed against the vendor library's own answers for the same fields: state of charge came
/// back 0 to 100 step 1, battery current -500 to 500, and the name and unit string ids resolved
/// through the string table to "Battery", "V", "A" and "%".
static constexpr uint8_t PROPERTY_REQUEST_TYPE = 0x31;
static constexpr uint8_t PROPERTY_INFORMATION_TYPE = 0x11;

enum class MasterbusProperty : uint8_t {
  MASTERBUS_PROPERTY_DISPLAY_TYPE = 0x02,
  MASTERBUS_PROPERTY_MINIMUM = 0x06,
  MASTERBUS_PROPERTY_MAXIMUM = 0x07,
  MASTERBUS_PROPERTY_STEP = 0x08,
  MASTERBUS_PROPERTY_NAME_STRING = 0x28,
  MASTERBUS_PROPERTY_UNIT_STRING = 0x2C,
};

/// VERIFIED: the display type is what decides which ESPHome platform a field belongs to.
/// Cross-checked against the vendor library's own value types over five fields: float fields
/// answered 1, time fields 7, a date field 8. Two boolean fields split - "Stop charge" answered 4
/// (checkbox, a state) while "Close relay" and "Open relay" answered 5 (button, a momentary
/// command) - a distinction the library itself does not expose.
///
/// UNVERIFIED: properties 0x09, 0x0B, 0x0C and 0x0D are flags. 0x0B and 0x0D are set together and
/// only on the button fields, which fits eventable rather than writable. The writable flag has not
/// been identified, and the display type answers the platform question without it.

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

/// VERIFIED: a monitoring group is walked the same way a field is, one selector at a time.
///
///     request   type 0x32, 3 bytes: selector, group number little-endian
///               type 0x32, 4 bytes: selector, group number, index - for the field list
///     answer    type 0x12, the header, one tag byte, then the value
///
/// Confirmed against what the vendor library reported for the same groups: a display panel's
/// group 1 answered six fields numbered 32 to 37, and a battery's group 0 answered seven fields
/// numbered 0, 4, 3, 1, 2, 5 and 168 - both lists matching the library exactly, in order. The
/// group name resolved through the string table to "Cluster", which is what the library calls it.
///
/// The field count arrives as a float, which is odd for a count but consistent across every group
/// seen.
static constexpr uint8_t GROUP_REQUEST_TYPE = 0x32;
static constexpr uint8_t GROUP_INFORMATION_TYPE = 0x12;

enum class MasterbusGroupSelector : uint8_t {
  MASTERBUS_GROUP_SELECTOR_FIELD_AT_INDEX = 0x03,
  MASTERBUS_GROUP_SELECTOR_FIELD_COUNT = 0x07,
  MASTERBUS_GROUP_SELECTOR_NAME_STRING = 0x28,
};

// ---------------------------------------------------------------------------
// Message numbering
// ---------------------------------------------------------------------------

/// VERIFIED as to layout, read out of the vendor's own shared library: the tab messages are one
/// flat list, and each entry exists three times over - as an answer at 0x10, as a refusal at 0x20
/// and as a request at 0x30. The library holds the names in that order, in three blocks with the
/// same internal sequence, which is what gives the list below.
///
/// The order is the vendor's and it is not regular. The alarm tab has no Data entry here at all -
/// the library files AlarmData with the broadcast messages instead - and the two remaining Data
/// entries sit out of line with their own tabs. So this is a list to look up, never an arithmetic
/// of tab and kind.
///
/// Three of the twelve were already decoded from traffic: monitoring's own. Three more then turned
/// up in a capture of an unrelated installation exactly where this ordering predicts them -
/// 0x34/0x14, 0x36/0x16 and 0x39/0x19, all carrying the group walk's framing, sent in one burst
/// the moment a display panel opened a device's menu. That is what turns the ordering from a
/// reading of a string table into a tested claim. The remaining six are marked DERIVED: they
/// follow from the same ordering and have not themselves been seen on a wire.
enum class MasterbusMessage : uint8_t {
  MASTERBUS_MESSAGE_MONITORING_DATA = 0,         // VERIFIED  0x30 / 0x10
  MASTERBUS_MESSAGE_MONITORING_PROPERTY = 1,     // VERIFIED  0x31 / 0x11
  MASTERBUS_MESSAGE_MONITORING_GROUP = 2,        // VERIFIED  0x32 / 0x12
  MASTERBUS_MESSAGE_ALARM_PROPERTY = 3,          // DERIVED   0x33 / 0x13
  MASTERBUS_MESSAGE_ALARM_GROUP = 4,             // VERIFIED  0x34 / 0x14
  MASTERBUS_MESSAGE_HISTORY_PROPERTY = 5,        // DERIVED   0x35 / 0x15
  MASTERBUS_MESSAGE_HISTORY_GROUP = 6,           // VERIFIED  0x36 / 0x16
  MASTERBUS_MESSAGE_CONFIGURATION_DATA = 7,      // DERIVED   0x37 / 0x17
  MASTERBUS_MESSAGE_CONFIGURATION_PROPERTY = 8,  // DERIVED   0x38 / 0x18
  MASTERBUS_MESSAGE_CONFIGURATION_GROUP = 9,     // VERIFIED  0x39 / 0x19
  MASTERBUS_MESSAGE_HISTORY_DATA = 10,           // DERIVED   0x3A / 0x1A
  MASTERBUS_MESSAGE_BOOTLOADER = 11,             // DERIVED   0x3B / 0x1B - out of scope, listed so
                                                 // the list is the vendor's and not an edited one
};

static constexpr uint8_t MESSAGE_INFORMATION_BASE = 0x10;
/// DERIVED: no refusal on a tab message has been captured. The string family refuses at
/// STRING_NOT_AVAILABLE_TYPE, one above its answer, so this base is the pattern applied and not a
/// measurement.
static constexpr uint8_t MESSAGE_NOT_AVAILABLE_BASE = 0x20;
static constexpr uint8_t MESSAGE_REQUEST_BASE = 0x30;
/// How many entries the vendor's list holds. Every one of them exists at each of the three bases.
static constexpr uint8_t MESSAGE_COUNT = 12;
static_assert(static_cast<uint8_t>(MasterbusMessage::MASTERBUS_MESSAGE_BOOTLOADER) + 1 == MESSAGE_COUNT,
              "the message count must follow the list, not be maintained beside it");

constexpr uint8_t masterbus_request_type(MasterbusMessage message) {
  return static_cast<uint8_t>(MESSAGE_REQUEST_BASE + static_cast<uint8_t>(message));
}
constexpr uint8_t masterbus_information_type(MasterbusMessage message) {
  return static_cast<uint8_t>(MESSAGE_INFORMATION_BASE + static_cast<uint8_t>(message));
}

// The three that were decoded from traffic long before the list was found. Asserting them here is
// what stops the list and the named constants above from drifting apart.
static_assert(masterbus_request_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_DATA) == MONITORING_REQUEST_TYPE,
              "monitoring data request must stay 0x30");
static_assert(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_DATA) ==
                  MONITORING_INFORMATION_TYPE,
              "monitoring data answer must stay 0x10");
static_assert(masterbus_request_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_PROPERTY) == PROPERTY_REQUEST_TYPE,
              "monitoring property request must stay 0x31");
static_assert(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_PROPERTY) ==
                  PROPERTY_INFORMATION_TYPE,
              "monitoring property answer must stay 0x11");
static_assert(masterbus_request_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_GROUP) == GROUP_REQUEST_TYPE,
              "monitoring group request must stay 0x32");
static_assert(masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_GROUP) ==
                  GROUP_INFORMATION_TYPE,
              "monitoring group answer must stay 0x12");

/// Whether a message type is one this component can name. Everything else is worth surfacing, and
/// that is the point of the test: the vendor names messages nobody here has ever seen a number
/// for - alarm data and the two bus messages among them - and the only way to learn where they sit
/// is to catch one in the act.
constexpr bool is_known_message_type(uint8_t type) {
  if (type >= MESSAGE_INFORMATION_BASE && type < MESSAGE_INFORMATION_BASE + MESSAGE_COUNT)
    return true;
  if (type >= MESSAGE_NOT_AVAILABLE_BASE && type < MESSAGE_NOT_AVAILABLE_BASE + MESSAGE_COUNT)
    return true;
  if (type >= MESSAGE_REQUEST_BASE && type < MESSAGE_REQUEST_BASE + MESSAGE_COUNT)
    return true;
  return type == DEVICE_ANNOUNCEMENT_TYPE || type == NODE_NOT_AVAILABLE_TYPE || type == NODE_REQUEST_TYPE ||
         type == STRING_INFORMATION_TYPE || type == STRING_NOT_AVAILABLE_TYPE || type == STRING_REQUEST_TYPE;
}

/// Whether a frame of this type is one the device itself sent. A request carries the address of
/// the device it is aimed at and nothing about its sender, so a frame addressed to a device says
/// only that somebody asked it something - never that it is there to answer.
constexpr bool device_sent_message(uint8_t type) {
  if (type >= MESSAGE_INFORMATION_BASE && type < MESSAGE_INFORMATION_BASE + MESSAGE_COUNT)
    return true;
  if (type >= MESSAGE_NOT_AVAILABLE_BASE && type < MESSAGE_NOT_AVAILABLE_BASE + MESSAGE_COUNT)
    return true;
  return type == DEVICE_ANNOUNCEMENT_TYPE || type == NODE_NOT_AVAILABLE_TYPE || type == STRING_INFORMATION_TYPE ||
         type == STRING_NOT_AVAILABLE_TYPE;
}

// ---------------------------------------------------------------------------
// Not decoded
// ---------------------------------------------------------------------------
//
// UNVERIFIED: how many monitoring groups a device has. The vendor API exposes it, and the
// announcement payload carries bytes past the address that have not been read - the count may
// well be in there.
//
// UNVERIFIED: what the frame that follows every write means. Its shape is recorded above; only its
// purpose is open.
//
// UNVERIFIED as to meaning, VERIFIED as to shape: three further request/answer pairs exist, and
// they carry the group walk above byte for byte - selector, then the number little-endian, and an
// answer of that header, a tag byte and a float.
//
//     0x34 / 0x14      0x36 / 0x16      0x39 / 0x19
//
// Only selector 0x07, the field count, has been seen on any of them. A display panel opening a
// device's menu asks 0x32 for the monitoring group count, then runs all three of these in one
// burst, and only then walks the monitoring fields - so they are near certainly the same walk over
// the device's other tabs. Which pair is which tab is not settled, and no *value* read on those
// tabs has been captured at all, which is why an entity outside the monitoring tab is refused
// rather than guessed at.
//
// That they count groups rather than describe fields is settled, because the two readings disagree
// and only one survives: read as field properties, 0x39 would be saying that the first field of a
// DC shunt - its state of charge - has a maximum of 7.
//
// UNKNOWN: which fields a device will accept a write for. No read-only flag has been found among
// the properties, so a write the device ignores looks exactly like one it acted on. The vendor's
// library names a "writeable" property, so one exists and has simply not been identified.
//
// UNKNOWN: how an alarm's value is carried. The vendor names AlarmDataReq, AlarmDataInfo and
// AlarmDataNa, so alarms can be asked for - but it files all three with NodeReq, NodeInfo and
// BusInfo rather than with the other tabs, which is where broadcast messages live. Their numbers
// are outside the tab list and have not been captured, because no alarm has been active on the
// reference bus while anything was recording.

// ---------------------------------------------------------------------------
// Bounds the configuration validates against
// ---------------------------------------------------------------------------
//
// Mirrored in __init__.py, which is where they are enforced.

/// The identifier gives the address 23 bits, so the mask above is also the largest address there
/// can be. Every device seen fits, and the vendor's own example data does not - its 28 bit BusID
/// must be a different number from the one on the wire.
static constexpr uint32_t MAX_DEVICE_ADDRESS = DEVICE_ADDRESS_MASK;

/// A field number occupies two bytes of the monitoring payload.
static constexpr uint16_t MAX_PARAM = 0xFFFF;

// ---------------------------------------------------------------------------
// The vendor's data model
// ---------------------------------------------------------------------------

/// Which tab of a device a field lives on. The vendor library asserts on device identifier, tab,
/// group and index together, so the tab is part of every field address.
///
/// Bootloader is deliberately absent: firmware update over MasterBus is out of scope.
enum class MasterbusTab : uint8_t {
  MASTERBUS_TAB_MONITORING = 0,
  MASTERBUS_TAB_ALARM = 1,
  MASTERBUS_TAB_HISTORY = 2,
  MASTERBUS_TAB_CONFIGURATION = 3,
};

/// Which message walks a tab's groups, and which reads one field's properties. Every tab has both.
/// These are switches rather than arithmetic because the vendor's list is not in tab order - see
/// the message numbering above.
constexpr MasterbusMessage group_message(MasterbusTab tab) {
  switch (tab) {
    case MasterbusTab::MASTERBUS_TAB_ALARM:
      return MasterbusMessage::MASTERBUS_MESSAGE_ALARM_GROUP;
    case MasterbusTab::MASTERBUS_TAB_HISTORY:
      return MasterbusMessage::MASTERBUS_MESSAGE_HISTORY_GROUP;
    case MasterbusTab::MASTERBUS_TAB_CONFIGURATION:
      return MasterbusMessage::MASTERBUS_MESSAGE_CONFIGURATION_GROUP;
    default:
      return MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_GROUP;
  }
}

constexpr MasterbusMessage property_message(MasterbusTab tab) {
  switch (tab) {
    case MasterbusTab::MASTERBUS_TAB_ALARM:
      return MasterbusMessage::MASTERBUS_MESSAGE_ALARM_PROPERTY;
    case MasterbusTab::MASTERBUS_TAB_HISTORY:
      return MasterbusMessage::MASTERBUS_MESSAGE_HISTORY_PROPERTY;
    case MasterbusTab::MASTERBUS_TAB_CONFIGURATION:
      return MasterbusMessage::MASTERBUS_MESSAGE_CONFIGURATION_PROPERTY;
    default:
      return MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_PROPERTY;
  }
}

/// Which tab an answer carrying a value belongs to, and whether it carries one at all. The four
/// tabs answer with the same payload shape, so the message number is the only thing that says
/// which tab a value came from - taking one for another would publish a field from the wrong tab.
constexpr bool data_information_tab(uint8_t type, MasterbusTab &out) {
  if (type == masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_DATA)) {
    out = MasterbusTab::MASTERBUS_TAB_MONITORING;
    return true;
  }
  if (type == masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_HISTORY_DATA)) {
    out = MasterbusTab::MASTERBUS_TAB_HISTORY;
    return true;
  }
  if (type == masterbus_information_type(MasterbusMessage::MASTERBUS_MESSAGE_CONFIGURATION_DATA)) {
    out = MasterbusTab::MASTERBUS_TAB_CONFIGURATION;
    return true;
  }
  return false;
}

/// The message that reads a field's value, and whether the tab has one at all. The alarm tab does
/// not: the vendor files AlarmData with the broadcast messages rather than with the tabs, and its
/// numbering is unknown. So a tab can be walked for what it contains without its values being
/// readable, which is exactly the state the alarm tab is in.
constexpr bool tab_data_message(MasterbusTab tab, MasterbusMessage &out) {
  switch (tab) {
    case MasterbusTab::MASTERBUS_TAB_MONITORING:
      out = MasterbusMessage::MASTERBUS_MESSAGE_MONITORING_DATA;
      return true;
    case MasterbusTab::MASTERBUS_TAB_HISTORY:
      out = MasterbusMessage::MASTERBUS_MESSAGE_HISTORY_DATA;
      return true;
    case MasterbusTab::MASTERBUS_TAB_CONFIGURATION:
      out = MasterbusMessage::MASTERBUS_MESSAGE_CONFIGURATION_DATA;
      return true;
    default:
      return false;
  }
}

/// How a device asks for a field to be displayed, and what decides which ESPHome platform the
/// field becomes. Read from property 0x02. The numbering is the vendor's own, confirmed against
/// its example device where field N carries display type N, and against a live bus where float,
/// time, date, checkbox and button fields all answered as listed here.
enum class MasterbusDisplayType : uint8_t {
  MASTERBUS_DISPLAY_TYPE_FLOAT = 1,
  MASTERBUS_DISPLAY_TYPE_RADIO = 2,
  MASTERBUS_DISPLAY_TYPE_DROPDOWN = 3,
  MASTERBUS_DISPLAY_TYPE_CHECKBOX = 4,
  MASTERBUS_DISPLAY_TYPE_BUTTON = 5,
  MASTERBUS_DISPLAY_TYPE_TEXT = 6,
  MASTERBUS_DISPLAY_TYPE_TIME = 7,
  MASTERBUS_DISPLAY_TYPE_DATE = 8,
  MASTERBUS_DISPLAY_TYPE_DEVICE_LIST = 9,
  MASTERBUS_DISPLAY_TYPE_EVENT = 10,
  MASTERBUS_DISPLAY_TYPE_SWITCH = 11,
};

/// The value types the vendor API distinguishes. An entity declares which one it expects, and the
/// declaration is what turns the float on the wire into a boolean or a list index.
enum class MasterbusValueType : uint8_t {
  MASTERBUS_VALUE_TYPE_FLOAT = 0,
  MASTERBUS_VALUE_TYPE_DATE = 1,
  MASTERBUS_VALUE_TYPE_TIME = 2,
  MASTERBUS_VALUE_TYPE_BOOLEAN = 3,
  MASTERBUS_VALUE_TYPE_LIST_OPTION = 4,
  MASTERBUS_VALUE_TYPE_TEXT = 5,
  MASTERBUS_VALUE_TYPE_DEVICE_ID = 6,
  MASTERBUS_VALUE_TYPE_EVENTABLE = 7,
  MASTERBUS_VALUE_TYPE_INVALID = 8,
};

/// The status a device reports for itself. Sleeping, off-fault and off-error are defined by the
/// vendor API but documented there as unused.
///
/// Nothing reads a reported status yet: a device is treated as on for as long as frames arrive
/// from it, because no message carrying the vendor's status field has been identified.
enum class MasterbusDeviceStatus : uint8_t {
  MASTERBUS_DEVICE_STATUS_OFFLINE = 0,
  MASTERBUS_DEVICE_STATUS_SLEEPING = 1,
  MASTERBUS_DEVICE_STATUS_ON = 2,
  MASTERBUS_DEVICE_STATUS_ON_WARNING = 3,
  MASTERBUS_DEVICE_STATUS_OFF_FAULT = 4,
  MASTERBUS_DEVICE_STATUS_OFF_ERROR = 5,
  MASTERBUS_DEVICE_STATUS_UPDATING = 6,
};

const char *masterbus_tab_to_string(MasterbusTab tab);

}  // namespace esphome::masterbus
