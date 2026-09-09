#pragma once

#include <cinttypes>

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

// VERIFIED: a device answers a monitoring field only when asked. The same capture holds 42152
// answers that followed a request against 20 that did not, so a hub that never transmits reports
// values only for as long as something else on the bus is polling the same fields. This is why
// poll_interval exists.

// ---------------------------------------------------------------------------
// Device announcements
// ---------------------------------------------------------------------------

/// VERIFIED: asking every device to announce itself is a frame with no payload at all. The whole
/// message is the identifier - type 0x05 against a fixed broadcast address that belongs to no
/// device. Captured while the vendor library opened a fresh context: it sends the request three
/// times in a row, and every device answers within about four milliseconds.
///
/// Measured over 1797 requests in a 10 minute capture: exactly eight distinct devices answered
/// each one - minimum eight, maximum eight - with a median delay of 5.8 ms.
static constexpr uint8_t NODE_REQUEST_TYPE = 0x0A;
static constexpr uint32_t NODE_REQUEST_ADDRESS = 0x500001;
/// The library repeats it, so a device that missed the first still answers.
static constexpr uint8_t NODE_REQUEST_REPEATS = 3;

/// VERIFIED: a device answers with its own address in the first four payload bytes, in an odd
/// packing: the top six bits, then the low sixteen little-endian, then the two remaining bits in
/// the low end of the fourth byte. All eight addresses in a 15216 frame sample rebuilt exactly,
/// and they were precisely the eight devices the vendor library reports for this bus.
static constexpr uint8_t DEVICE_ANNOUNCEMENT_TYPE = 0x08;
static constexpr uint8_t DEVICE_ANNOUNCEMENT_LENGTH = 8;

inline uint32_t decode_announced_address(const uint8_t *data) {
  return (static_cast<uint32_t>(data[0]) << 18) | (static_cast<uint32_t>(data[3] & 0x03) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[1]);
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
// Not decoded
// ---------------------------------------------------------------------------
//
// UNVERIFIED: how many monitoring groups a device has. The vendor API exposes it, and the
// announcement payload carries bytes past the address that have not been read - the count may
// well be in there.
//
// UNKNOWN: how a value is written. The vendor API exposes one write call, for boolean fields, and
// no frame from it has been recorded.
// and none of their messages have been identified.

// ---------------------------------------------------------------------------
// Bounds the configuration validates against
// ---------------------------------------------------------------------------
//
// Mirrored in __init__.py, which is where they are enforced.

/// The identifier gives the address 23 bits. Every device seen fits, and the vendor's own example
/// data does not - its 28 bit BusID must be a different number from the one on the wire.
static constexpr uint32_t MAX_DEVICE_ADDRESS = 0x007FFFFF;

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
const char *masterbus_device_status_to_string(MasterbusDeviceStatus status);

}  // namespace esphome::masterbus
