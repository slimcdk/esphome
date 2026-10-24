#pragma once

#include <algorithm>
#include <cinttypes>
#include <cstddef>

namespace esphome::masterbus {

// The MasterBus protocol, in two halves.
//
// The wire format below is not published. It was derived by watching a live installation and by
// reading what the vendor's own library does. Each constant carries how far it is actually known,
// so that confirming one is a change to this file alone:
//
//   VERIFIED    Measured against captured traffic, or read out of the vendor SDK's data files.
//   LIBRARY     What the vendor's software sends, accepts or names - its MasterBus library, or its
//               configuration tool - read out of that software rather than seen on a wire. Strong,
//               but no device has yet been watched doing it.
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

/// VERIFIED: the top five bits of an address say what kind of equipment the device is. The vendor's
/// configuration tool names them, and every device on two installations - eleven of eleven - sits
/// in the class its own name says: interfaces in 14, display panels in 20, battery monitors in 27.
/// VERIFIED: the low 18 bits are worked out from the unit's serial and article numbers. The vendor's
/// configuration tool holds the formula, and it gives the address of every device on the reference
/// bus from the numbers on its label.
static constexpr uint8_t DEVICE_CLASS_SHIFT = 18;

constexpr uint8_t device_class(uint32_t address) { return (address & DEVICE_ADDRESS_MASK) >> DEVICE_CLASS_SHIFT; }

/// VERIFIED: the address this hub speaks as, which is the vendor library's own. The library asks
/// for announcements under it, and every device answers a node request except the one whose
/// address it carries, so this is a node identity and not a broadcast address. It is also what the
/// library writes after every write it makes, as the node that made the change (see below).
static constexpr uint32_t HUB_ADDRESS = 0x500001;

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
/// ends up on the wire. Asking device 0x6C1001 for field 117 is (0x30 << 23) | 0x6C1001, which
/// reads as 186C1001#7500 in a candump log - not 306C1001.
static constexpr uint8_t MONITORING_REQUEST_TYPE = 0x30;
static constexpr uint8_t MONITORING_INFORMATION_TYPE = 0x10;
static constexpr uint8_t MONITORING_REQUEST_LENGTH = 2;
static constexpr uint8_t MONITORING_INFORMATION_LENGTH = 6;
// VERIFIED on every tab: each reads a value with message numbers of its own (see tab_message()
// below) and the two lengths above. Configuration reads were answered in exactly this shape on a
// live bus, by a battery block and a display panel, for floats, list options and text; alarm and
// history reads by a battery block, its alarm flags and its running days and charge cycles. An
// answer of a different length is dropped rather than misread, so equipment that answers otherwise
// shows up as silence, not as a wrong value.

/// VERIFIED: a monitoring field is written with the same message type that reads it. What tells
/// the two apart is the payload length: two bytes asks, six bytes writes. LIBRARY: every other tab
/// is written the same way, on its own request number.
///
///     write     type 0x30, 6 bytes: field number little-endian, then float32 little-endian
///
/// Captured while driving the vendor library's own "Open relay" and "Close relay" calls against a
/// battery block: twelve writes, all of this shape, and the relay opened on the first of them.
/// VERIFIED for a number too: 40.0 written to a display panel's Backlight, a percentage, was read
/// back as 40, and the field after it was left as it was.
static constexpr uint8_t MONITORING_WRITE_LENGTH = 6;

/// VERIFIED as to the bytes, LIBRARY as to their meaning: a write to an event field is followed by a
/// second one, to the field one higher, whose value is the address of the node that wrote -
/// HUB_ADDRESS, as a 32 bit number little-endian. The vendor library always sends its own, which is
/// why the same four bytes, 01 00 50 00, followed a write to field 117 and to field 119. Neither
/// higher field is in the device's own field list: the pair records which node set an event off.
///
/// VERIFIED that this is for events only. The library writes nothing but booleans to events, and on
/// the reference bus the only fields a device calls eventable (property 0x0D) are the battery
/// blocks' 117 and 119. Every other writable field has a field of its own one number up: a display
/// panel's Backlight is 42 and its Page duration 43, its Close relay switch 32 and its Open relay
/// switch 33. So the second frame goes only to a field the device says is an event.

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
/// carrying a real device's address were answered by exactly the seven others, which is why the
/// hub asks under HUB_ADDRESS. Captured while the vendor library opened a fresh context: it sends
/// the request three times in a row, and every device answers within about four milliseconds.
///
/// Measured over 1797 requests in a 10 minute capture: exactly eight distinct devices answered
/// each one - minimum eight, maximum eight - with a median delay of 5.8 ms.
static constexpr uint8_t NODE_REQUEST_TYPE = 0x0A;
// LIBRARY: the vendor calls this pair BusReq and BusInfo, and it has no refusal. What the vendor
// calls NodeReq, NodeInfo and NodeNa is the family of 0x0E, 0x0C and 0x0D below. 0x09 is nothing.
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
/// LIBRARY: the remaining four bytes are, in the vendor's words, the device's component (byte 4),
/// power type (byte 5, 0 to 3) and status version (byte 7, low five bits). Bit 5 of byte 6 asks to
/// be read again: while it is set the vendor library rediscovers the device on every announcement.
/// There is no device status in them. Run against a pretend bus the library reported "on" for every
/// pattern tried, and for 150 seconds after the device fell silent, so silence is the only status
/// there is to copy.
///
/// VERIFIED: an announcement is an answer, never sent unprompted. A 10 minute capture held exactly
/// as many announcements as the node requests in it called for - 1797 requests answered by eight
/// devices and 120 by seven, 15216 in all, not one left over. On a bus where this hub is the only
/// node, nothing announces itself until the hub asks.
static constexpr uint8_t DEVICE_ANNOUNCEMENT_TYPE = 0x08;
static constexpr uint8_t DEVICE_ANNOUNCEMENT_LENGTH = 8;
static constexpr uint8_t ANNOUNCEMENT_STATUS_VERSION_MASK = 0x1F;
static constexpr uint8_t ANNOUNCEMENT_READ_AGAIN_BIT = 0x20;

inline uint32_t decode_announced_address(const uint8_t *data) {
  const uint32_t address = (static_cast<uint32_t>(data[0]) << 18) | (static_cast<uint32_t>(data[3] & 0x03) << 16) |
                           (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[1]);
  return address & DEVICE_ADDRESS_MASK;
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

/// VERIFIED: a device holds its text in a numbered string table, read four bytes at a time. The
/// vendor calls the question ProdString, one of the family of NodeReq (0x0E) told apart by its
/// first byte.
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
/// `data` is a whole string answer, header included. `out` is always left NUL terminated. Where
/// `truncated` is given it is set if a byte had to be dropped, so that a caller can say so rather
/// than report a cut name as though the device sent it that way.
inline bool take_string_chunk(const uint8_t *data, size_t size, char *out, uint8_t capacity,
                              bool *truncated = nullptr) {
  // A short answer is the last one: the device sends four bytes of text until it runs out.
  bool ended = size < STRING_CHUNK_LENGTH + 4;
  size_t at = static_cast<size_t>(data[3]) * 4;
  for (size_t i = STRING_CHUNK_LENGTH; i < size; i++) {
    if (data[i] == 0) {
      ended = true;
      break;
    }
    if (at < static_cast<size_t>(capacity) - 1) {
      out[at++] = static_cast<char>(data[i]);
    } else if (truncated != nullptr) {
      *truncated = true;
    }
  }
  out[std::min<size_t>(at, capacity - 1)] = '\0';
  return ended;
}

/// Whether a string that has not ended by this chunk has filled a buffer of `capacity`, so that
/// the next chunk could only be dropped. Nothing bounds a string but its terminator, so a reader
/// stops here and reports the text as cut short rather than asking for ever.
constexpr bool string_buffer_full(uint8_t chunk, uint8_t capacity) {
  return (static_cast<size_t>(chunk) + 1) * 4 >= static_cast<size_t>(capacity) - 1;
}

// ---------------------------------------------------------------------------
// General counts
// ---------------------------------------------------------------------------

/// VERIFIED as to shape, LIBRARY as to the names: a device answers a two byte question with a 16 bit
/// number. The vendor calls the family GeneralCount. It shares the string table's message numbers
/// and is told apart by its first byte, the way a write is told from a read by its length.
///
///     request   type 0x0E, 2 bytes: 0x08, which count
///     answer    type 0x0C, 4 bytes: the two byte header, then the number little-endian
///     refused   type 0x0D, 2 bytes: the header alone
///
/// The vendor's configuration tool names every question the vendor library accepts. The ones this
/// component asks:
///
///     08 02   monitoring groups      08 04   history groups
///     08 06   configuration groups   08 08   alarm groups
///     08 3F   eventables
///
/// VERIFIED: the group counts. In a 2022 capture a display panel asked all four of a MassCombi and a
/// MasterShunt and then walked what it was told: eight of eight matched the number of groups each
/// device went on to answer for. A device that refuses one is walked the older way, by asking for
/// the group after the last one and taking its silence as the end of the list.
///
/// VERIFIED as numbers: eventables, how many event slots a device has. All six lithium battery
/// blocks answer 2, a display panel 16; the 2022 installation's MassCombi interface 8 and its
/// MasterView 11, and a MasterShunt refuses it. It was once read here as a product code, because
/// identical units agree on it; the device class in the address is what says what a device is.
static constexpr uint8_t GENERAL_COUNT_SELECTOR = 0x08;
static constexpr uint8_t GENERAL_COUNT_REQUEST_LENGTH = 2;
static constexpr uint8_t GENERAL_COUNT_INFORMATION_LENGTH = 4;

static constexpr uint8_t GENERAL_COUNT_MONITORING_GROUPS = 0x02;
static constexpr uint8_t GENERAL_COUNT_HISTORY_GROUPS = 0x04;
static constexpr uint8_t GENERAL_COUNT_CONFIGURATION_GROUPS = 0x06;
static constexpr uint8_t GENERAL_COUNT_ALARM_GROUPS = 0x08;
static constexpr uint8_t GENERAL_COUNT_EVENTABLES = 0x3F;

/// Whether a frame of the string family answers one of these questions rather than carrying a
/// chunk of text. Both arrive under the same message number, and the first two bytes are what
/// tell them apart.
inline bool is_general_count_header(const uint8_t *data, size_t size, uint8_t question) {
  return size >= GENERAL_COUNT_REQUEST_LENGTH && data[0] == GENERAL_COUNT_SELECTOR && data[1] == question;
}

/// LIBRARY: the counts the vendor library accepts an answer to, and no others. Its configuration
/// tool names every one of them: the field and group counts of each tab (0x01-0x08), software and
/// bootloader versions, the menu and string versions a client caches against (0x12, 0x13,
/// 0x1B-0x1F), the alarm counts (0x20, 0x21), language, clock and calendar, eventables (0x3F) and
/// one count per eventable (0x40-0x5F).
constexpr bool is_general_count_question(uint8_t question) {
  return (question >= 0x01 && question <= 0x08) || (question >= 0x0A && question <= 0x0E) ||
         (question >= 0x11 && question <= 0x13) || (question >= 0x18 && question <= 0x27) || question == 0x2A ||
         (question >= 0x3E && question <= 0x60);
}
static constexpr uint8_t GENERAL_COUNT_LAST_QUESTION = 0x60;

/// LIBRARY, by the vendor's library and its configuration tool alike: a device runs one or more
/// processors, and each answers for its software version under a question of the same family.
///
///     request   type 0x0E, 2 bytes: 0x82, which processor (0, 1, ...)
///     answer    type 0x0C, 6 bytes: the two, the version little-endian, then two zero bytes
///     refused   type 0x0D, 2 bytes: the two alone - one past the last processor
///
/// The version reads as its high byte, a dot and its low byte. The vendor library lists the
/// processors by asking from 0 until a device refuses one.
static constexpr uint8_t SOFTWARE_VERSION_SELECTOR = 0x82;
static constexpr uint8_t SOFTWARE_VERSION_INFORMATION_LENGTH = 4;

inline bool is_software_version_header(const uint8_t *data, size_t size, uint8_t processor) {
  return size >= 2 && data[0] == SOFTWARE_VERSION_SELECTOR && data[1] == processor;
}

/// LIBRARY: a device's article number, serial number, product name and revision code are strings in
/// its own table, and a question of the same family says which entry holds each one.
///
///     request   type 0x0E, 2 bytes: 0x09, which string (1 to 4)
///     answer    type 0x0C, 4 bytes: those two, then the string id little-endian
///     refused   type 0x0D, 2 bytes: the two alone
///
/// The vendor library asks all four of every device it finds, before it lists the device at all.
/// VERIFIED: what the vendor calls the product name is the name the device carries - six identical
/// battery blocks answered with the six names they were given at installation - and the article
/// number is the one on the label.
/// 09 03 was once read here as the highest string id a table holds. It is the name's id, which on
/// both devices measured was the last string in the table.
static constexpr uint8_t GENERAL_STRING_SELECTOR = 0x09;
static constexpr uint8_t GENERAL_STRING_INFORMATION_LENGTH = 4;
static constexpr uint8_t GENERAL_STRING_ARTICLE_NUMBER = 1;
static constexpr uint8_t GENERAL_STRING_SERIAL_NUMBER = 2;
static constexpr uint8_t GENERAL_STRING_PRODUCT_NAME = 3;
static constexpr uint8_t GENERAL_STRING_REVISION_CODE = 4;

inline bool is_general_string_header(const uint8_t *data, size_t size, uint8_t question) {
  return size >= 2 && data[0] == GENERAL_STRING_SELECTOR && data[1] == question;
}

// ---------------------------------------------------------------------------
// Field properties
// ---------------------------------------------------------------------------

/// VERIFIED: a field's metadata is read one property at a time.
///
///     request   type 0x31, 3 bytes: property id, field number little-endian
///               type 0x31, 4 bytes: the same, then an index - for the properties that are lists
///     answer    type 0x11, the 3 byte header, the index (0 where there is none), then the value
///
/// The value is a float32 for the numeric properties and a little-endian 16 bit number for the
/// rest. A property a field does not have simply goes unanswered - a boolean field returns nothing
/// for minimum, maximum, step or unit. LIBRARY: the vendor library keeps the fourth byte for the
/// list properties only, which is what makes it an index rather than the tag it was once read as.
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
  MASTERBUS_PROPERTY_FACTORY = 0x09,
  MASTERBUS_PROPERTY_WRITEABLE = 0x0B,
  MASTERBUS_PROPERTY_EVENTABLE = 0x0D,
  MASTERBUS_PROPERTY_LONG_NAME_STRING = 0x29,
  MASTERBUS_PROPERTY_VERY_LONG_NAME_STRING = 0x2A,
  // The list properties, asked with an index: an option of a radio or dropdown field, an entry of
  // a device list, and an entry of an event field.
  MASTERBUS_PROPERTY_OPTION_STRING = 0x26,
  MASTERBUS_PROPERTY_LIST_DEVICE = 0x20,
  MASTERBUS_PROPERTY_EVENT_FIELD = 0x0F,
};

/// VERIFIED on the reference bus, which answered all three for 153 list fields: what the list
/// properties hold.
///
///     0x26, index   the string id of that option of a radio or dropdown field (u16)
///     0x20, index   that entry of a device list: a device's address, packed in four bytes the way
///                   an announcement packs it
///     0x20, 0       for an event field, the device whose eventables it lists, packed the same way
///     0x0F, index   that entry of an event field: an eventable number on that device (one byte,
///                   then a byte the vendor library ignores)
///
/// The field's maximum (0x07) is how many entries there are. The vendor's configuration tool names
/// 0x26 List_Text and 0x29 / 0x2A Text_Value_Long and Text_Value_Very_Long - longer forms of the
/// name 0x28 carries - and leaves 0x0F and 0x20 unnamed.
///
/// VERIFIED, and what names an eventable: a device's count 0x40 + n is the field number of its
/// eventable n. Six battery blocks answer 0x40 with 117 and 0x41 with 119, their Close relay and
/// Open relay buttons. Counts 0x22, 0x24, 0x25 and 0x60 point at fields the same way - the
/// language setting, the clock, the calendar and the cluster master on a battery block - and 0x3E
/// at the group that holds the events.
static constexpr uint8_t PROPERTY_INDEX_OFFSET = 3;
static constexpr uint8_t PROPERTY_VALUE_OFFSET = 4;

/// VERIFIED: the display type is what decides which ESPHome platform a field belongs to.
/// Cross-checked against the vendor library's own value types over five fields: float fields
/// answered 1, time fields 7, a date field 8. Two boolean fields split - "Stop charge" answered 4
/// (checkbox, a state) while "Close relay" and "Open relay" answered 5 (button, a momentary
/// command) - a distinction the library itself does not expose.
///
/// LIBRARY: property 0x09 carries a 32 bit value, the factory default by elimination, and 0x0B, 0x0C
/// and 0x0D are booleans - a 16 bit number that is set or not. They match the three booleans of
/// the vendor's own field description: writeable, GrayVisualization and eventable.
///
/// VERIFIED: 0x0B is writeable and 0x0D is eventable. A button answers both. A display panel's
/// event settings on its configuration tab - which source, target, command and data each of its
/// events uses - answered 0x0B set and 0x0D clear, every one of them: settings, written but not
/// events. LIBRARY: the vendor's configuration tool names 0x0C the greyed-out flag.
///
/// The vendor's tool asks a float field for its minimum, maximum, step and unit and expects them;
/// of every other field it takes silence for minimum 0, maximum 1, step 1 and no unit. It expects
/// eventable only on the monitoring tab, of dropdowns, checkboxes, buttons and switches.

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

/// VERIFIED: a monitoring group is walked the same way a field is, one selector at a time.
///
///     request   type 0x32, 3 bytes: selector, group number little-endian
///               type 0x32, 4 bytes: selector, group number, index - for the field list
///     answer    type 0x12, the header, the index, then the value
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
// Bounds the configuration validates against
// ---------------------------------------------------------------------------
//
// Mirrored in __init__.py, which is where they are enforced.

/// The identifier gives the address 23 bits, so the mask above is also the largest address there
/// can be. The BusID in the vendor's own example data is the same address held another way: the
/// class moved up to bit 24, the low 18 bits where they were.
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

/// Every tab, for walking them all.
static constexpr MasterbusTab MASTERBUS_TABS[] = {
    MasterbusTab::MASTERBUS_TAB_MONITORING,
    MasterbusTab::MASTERBUS_TAB_ALARM,
    MasterbusTab::MASTERBUS_TAB_HISTORY,
    MasterbusTab::MASTERBUS_TAB_CONFIGURATION,
};

// ---------------------------------------------------------------------------
// Message numbering
// ---------------------------------------------------------------------------

/// One message as it exists three times over: the request a node sends, the answer a device gives,
/// and the refusal it gives instead.
struct MasterbusMessageNumbers {
  uint8_t request;
  uint8_t information;
  uint8_t not_available;

  constexpr bool contains(uint8_t type) const {
    return type == this->request || type == this->information || type == this->not_available;
  }
};

/// What a tab message is about: a field's value, one property of a field, or one of a group.
enum class MasterbusTabMessage : uint8_t {
  MASTERBUS_TAB_MESSAGE_DATA,
  MASTERBUS_TAB_MESSAGE_PROPERTY,
  MASTERBUS_TAB_MESSAGE_GROUP,
};

static constexpr MasterbusTabMessage MASTERBUS_TAB_MESSAGES[] = {
    MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA,
    MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_PROPERTY,
    MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_GROUP,
};

/// LIBRARY as a table, VERIFIED entry by entry where it says so: the vendor library builds every
/// tab message from the tab and what is asked about it, and its decoder accepts exactly these
/// numbers.
///
///                     value             property          group
///     monitoring      30 / 10 / 20      31 / 11 / 21      32 / 12 / 22
///     alarm           06 / 04 / 05      33 / 13 / 23      34 / 14 / 24
///     history         3B / 1B / 2B      35 / 15 / 25      36 / 16 / 26
///     configuration   37 / 17 / 27      38 / 18 / 28      39 / 19 / 29
///
/// Each is request / answer / refusal. Properties and groups are arithmetic in the tab - two apart
/// per tab for monitoring, alarm and history - and configuration is the exception. Values follow no
/// rule: the library looks them up, and the alarm tab's sit down among the node messages.
///
/// VERIFIED on a wire: monitoring's three, the requests and answers of the other three tabs' values,
/// and those of the other three group walks, which a display panel sends in one burst when it opens
/// a device's menu. The library is what says which walk belongs to which tab. No refusal of a tab
/// message has been captured: a device that will not answer stays silent.
///
/// An earlier reading took the order the library stores its message names in for their numbering,
/// and put history values at 0x3A. The library refuses 0x3A.
constexpr MasterbusMessageNumbers tab_message(MasterbusTab tab, MasterbusTabMessage message) {
  switch (message) {
    case MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA:
      switch (tab) {
        case MasterbusTab::MASTERBUS_TAB_ALARM:
          return {0x06, 0x04, 0x05};
        case MasterbusTab::MASTERBUS_TAB_HISTORY:
          return {0x3B, 0x1B, 0x2B};
        case MasterbusTab::MASTERBUS_TAB_CONFIGURATION:
          return {0x37, 0x17, 0x27};
        default:
          return {0x30, 0x10, 0x20};
      }
    case MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_PROPERTY:
      switch (tab) {
        case MasterbusTab::MASTERBUS_TAB_ALARM:
          return {0x33, 0x13, 0x23};
        case MasterbusTab::MASTERBUS_TAB_HISTORY:
          return {0x35, 0x15, 0x25};
        case MasterbusTab::MASTERBUS_TAB_CONFIGURATION:
          return {0x38, 0x18, 0x28};
        default:
          return {0x31, 0x11, 0x21};
      }
    default:
      switch (tab) {
        case MasterbusTab::MASTERBUS_TAB_ALARM:
          return {0x34, 0x14, 0x24};
        case MasterbusTab::MASTERBUS_TAB_HISTORY:
          return {0x36, 0x16, 0x26};
        case MasterbusTab::MASTERBUS_TAB_CONFIGURATION:
          return {0x39, 0x19, 0x29};
        default:
          return {0x32, 0x12, 0x22};
      }
  }
}

// The three that were decoded from traffic long before the table was known. Asserting them here is
// what stops the table and the named constants above from drifting apart.
static_assert(tab_message(MasterbusTab::MASTERBUS_TAB_MONITORING, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA)
                          .request == MONITORING_REQUEST_TYPE &&
                  tab_message(MasterbusTab::MASTERBUS_TAB_MONITORING, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA)
                          .information == MONITORING_INFORMATION_TYPE,
              "monitoring data must stay 0x30 / 0x10");
static_assert(
    tab_message(MasterbusTab::MASTERBUS_TAB_MONITORING, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_PROPERTY).request ==
            PROPERTY_REQUEST_TYPE &&
        tab_message(MasterbusTab::MASTERBUS_TAB_MONITORING, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_PROPERTY)
                .information == PROPERTY_INFORMATION_TYPE,
    "monitoring property must stay 0x31 / 0x11");
static_assert(tab_message(MasterbusTab::MASTERBUS_TAB_MONITORING, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_GROUP)
                          .request == GROUP_REQUEST_TYPE &&
                  tab_message(MasterbusTab::MASTERBUS_TAB_MONITORING, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_GROUP)
                          .information == GROUP_INFORMATION_TYPE,
              "monitoring group must stay 0x32 / 0x12");

/// LIBRARY: what the vendor calls Parallel, presumably between devices that run in parallel. Its
/// library accepts the number and never looks inside, and nothing says which way it runs.
static constexpr uint8_t PARALLEL_MESSAGE_TYPE = 0x02;
/// LIBRARY: accepted and never looked inside either - the bootloader's, by the vendor's own name.
/// Firmware update over MasterBus is out of scope.
static constexpr MasterbusMessageNumbers BOOTLOADER_MESSAGE{0x3D, 0x1D, 0x2D};

/// LIBRARY: whether the vendor library would accept a message of this type. It refuses every other
/// number, so a frame that fails this is not MasterBus as the vendor's library speaks it. The
/// vendor's configuration tool knows three more, 0x1A / 0x2A / 0x3A as "connection config", which
/// are left unknown here so that one turning up is reported.
constexpr bool is_known_message_type(uint8_t type) {
  if (type == PARALLEL_MESSAGE_TYPE || type == DEVICE_ANNOUNCEMENT_TYPE || type == NODE_REQUEST_TYPE ||
      type == STRING_INFORMATION_TYPE || type == STRING_NOT_AVAILABLE_TYPE || type == STRING_REQUEST_TYPE ||
      BOOTLOADER_MESSAGE.contains(type))
    return true;
  for (MasterbusTab tab : MASTERBUS_TABS) {
    for (MasterbusTabMessage message : MASTERBUS_TAB_MESSAGES) {
      if (tab_message(tab, message).contains(type))
        return true;
    }
  }
  return false;
}

/// Whether a frame of this type is one the device itself sent. A request carries the address of
/// the device it is aimed at and nothing about its sender, so a frame addressed to a device says
/// only that somebody asked it something - never that it is there to answer. The Parallel message
/// is left out, because which way it runs is not known.
constexpr bool device_sent_message(uint8_t type) {
  if (type == DEVICE_ANNOUNCEMENT_TYPE || type == STRING_INFORMATION_TYPE || type == STRING_NOT_AVAILABLE_TYPE ||
      type == BOOTLOADER_MESSAGE.information || type == BOOTLOADER_MESSAGE.not_available)
    return true;
  for (MasterbusTab tab : MASTERBUS_TABS) {
    for (MasterbusTabMessage message : MASTERBUS_TAB_MESSAGES) {
      const MasterbusMessageNumbers numbers = tab_message(tab, message);
      if (type == numbers.information || type == numbers.not_available)
        return true;
    }
  }
  return false;
}

/// Which of the device property questions asks how many groups a tab holds. The numbering is the
/// vendor's and is not in tab order, so this is a switch rather than arithmetic.
constexpr uint8_t group_count_question(MasterbusTab tab) {
  switch (tab) {
    case MasterbusTab::MASTERBUS_TAB_ALARM:
      return GENERAL_COUNT_ALARM_GROUPS;
    case MasterbusTab::MASTERBUS_TAB_HISTORY:
      return GENERAL_COUNT_HISTORY_GROUPS;
    case MasterbusTab::MASTERBUS_TAB_CONFIGURATION:
      return GENERAL_COUNT_CONFIGURATION_GROUPS;
    default:
      return GENERAL_COUNT_MONITORING_GROUPS;
  }
}

/// Which tab an answer carrying a value belongs to, and whether it carries one at all. The four
/// tabs answer with the same payload shape, so the message number is the only thing that says
/// which tab a value came from - taking one for another would publish a field from the wrong tab.
constexpr bool data_information_tab(uint8_t type, MasterbusTab &out) {
  for (MasterbusTab tab : MASTERBUS_TABS) {
    if (tab_message(tab, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA).information == type) {
      out = tab;
      return true;
    }
  }
  return false;
}

/// The same for an answer to a property question.
constexpr bool property_information_tab(uint8_t type, MasterbusTab &out) {
  for (MasterbusTab tab : MASTERBUS_TABS) {
    if (tab_message(tab, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_PROPERTY).information == type) {
      out = tab;
      return true;
    }
  }
  return false;
}

/// The same for the request that reads a value - and, six bytes long, writes one.
constexpr bool data_request_tab(uint8_t type, MasterbusTab &out) {
  for (MasterbusTab tab : MASTERBUS_TABS) {
    if (tab_message(tab, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA).request == type) {
      out = tab;
      return true;
    }
  }
  return false;
}

/// Whether a frame is one this component reads past without understanding, and that a bus does
/// not carry as a matter of course. That is a type the vendor library would refuse, the Parallel
/// and bootloader messages nobody looks inside, and a value message whose length is neither a read
/// (two bytes) nor an answer (six). On a read's number that means a write or the frame after one,
/// sent by another node, because a controller never hears its own. Reads, answers, walks and
/// refusals are what a display panel produces all day, and reporting them would bury the frame
/// worth seeing.
constexpr bool is_undecoded_frame(uint8_t type, size_t length) {
  if (!is_known_message_type(type) || type == PARALLEL_MESSAGE_TYPE || BOOTLOADER_MESSAGE.contains(type))
    return true;
  MasterbusTab tab;
  if (data_information_tab(type, tab))
    return length != MONITORING_INFORMATION_LENGTH;
  if (data_request_tab(type, tab))
    return length != MONITORING_REQUEST_LENGTH;
  return false;
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
/// A device is treated as on for as long as frames arrive from it. The vendor library never sets
/// anything but "on" either (see the announcement above), so there is no status to read.
enum class MasterbusDeviceStatus : uint8_t {
  MASTERBUS_DEVICE_STATUS_OFFLINE = 0,
  MASTERBUS_DEVICE_STATUS_SLEEPING = 1,
  MASTERBUS_DEVICE_STATUS_ON = 2,
  MASTERBUS_DEVICE_STATUS_ON_WARNING = 3,
  MASTERBUS_DEVICE_STATUS_OFF_FAULT = 4,
  MASTERBUS_DEVICE_STATUS_OFF_ERROR = 5,
  MASTERBUS_DEVICE_STATUS_UPDATING = 6,
};

}  // namespace esphome::masterbus
