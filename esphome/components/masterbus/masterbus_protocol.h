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
/// address, so the mask is also the largest address there can be. Read this way, every message
/// type carries a payload of a near-fixed length.
static constexpr uint32_t DEVICE_ADDRESS_MASK = 0x007FFFFF;
static constexpr uint8_t MESSAGE_TYPE_SHIFT = 23;

/// VERIFIED: the top five bits of an address say what kind of equipment the device is, and the low
/// 18 bits are worked out from the unit's serial and article numbers, so a unit keeps its address
/// for life. The vendor's configuration tool names the classes and holds the formula.
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
/// number and a little-endian 32 bit float.
///
///     request   type 0x30, 2 bytes: field number little-endian
///     response  type 0x10, 6 bytes: field number little-endian, then float32 little-endian
///
/// The type is the value before the shift: asking device 0x6C1001 for field 117 puts
/// (0x30 << 23) | 0x6C1001 on the wire, which a candump log shows as 186C1001#7500.
static constexpr uint8_t MONITORING_REQUEST_TYPE = 0x30;
static constexpr uint8_t MONITORING_INFORMATION_TYPE = 0x10;
static constexpr uint8_t MONITORING_REQUEST_LENGTH = 2;
static constexpr uint8_t MONITORING_INFORMATION_LENGTH = 6;
// VERIFIED on every tab: each reads a value with message numbers of its own (see tab_message())
// and the two lengths above. An answer of another length is dropped rather than misread.
//
// VERIFIED: a device answers a field only when asked, so a hub that never transmits sees values
// only while something else on the bus polls the same fields. That is what an entity's
// update_interval is for.

// VERIFIED: a field is written with the message number that reads it, and the payload length tells
// the two apart: two bytes asks, six bytes writes. LIBRARY: every tab is written this way.
//
//     write     type 0x30, 6 bytes: field number little-endian, then float32 little-endian
//
// VERIFIED as to the bytes, LIBRARY as to their meaning: a write to an event field is followed by a
// second one, to the field one higher, whose value is the address of the node that wrote, as a 32
// bit number little-endian. Only an event gets it: every other field has a field of its own one
// number up, so the second frame goes only to a field the device says is an event.
//
// VERIFIED: a write that goes out correctly is not a command that took effect. Equipment can be
// shared between devices, and then only one of them acts on the command for it: two battery blocks
// that share a contactor both open it, but only one of them closes it again. So a caller that needs
// the result reads the field back. Which device owns what belongs to the installation, and so to
// the user's configuration; this component neither retries nor guesses.

// ---------------------------------------------------------------------------
// Dates and times
// ---------------------------------------------------------------------------

/// VERIFIED: a date and a time arrive as the ordinary monitoring float. A time counts seconds. A
/// date packs the calendar into one number, with months of 32 days and years of 13 months so that
/// the parts come back out by division:
///
///     year * 416 + month * 32 + day
///
/// A time is not always a clock: the same display type serves a time of day and a span of hours.
/// So the hour is not folded into a day, and both render as h:mm:ss.
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
/// which also catches the NaN a device that has no answer reports, and a month or day of zero,
/// which is what a date the device leaves unset unpacks to.
inline bool decode_date(float value, MasterbusDate &out) {
  // The year has to survive the cast as well as the packing, so the bound is the largest year the
  // struct can hold rather than the largest float.
  if (!(value >= 0.0f) || value >= static_cast<float>(DATE_MONTHS_PER_YEAR * DATE_DAYS_PER_MONTH) * 65536.0f)
    return false;
  const uint32_t packed = static_cast<uint32_t>(value);
  const uint32_t months = packed / DATE_DAYS_PER_MONTH;
  const auto month = static_cast<uint8_t>(months % DATE_MONTHS_PER_YEAR);
  const auto day = static_cast<uint8_t>(packed % DATE_DAYS_PER_MONTH);
  if (month == 0 || day == 0)
    return false;
  out.year = static_cast<uint16_t>(months / DATE_MONTHS_PER_YEAR);
  out.month = month;
  out.day = day;
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

/// VERIFIED: asking every device to announce itself is a frame with no payload, the node request
/// type against the address of whoever asks. Every device answers it, within a few milliseconds,
/// except the one whose address it carries, which is why the hub asks under HUB_ADDRESS. LIBRARY:
/// the vendor calls the pair BusReq and BusInfo, and it has no refusal; its NodeReq family is 0x0E,
/// 0x0C and 0x0D below.
static constexpr uint8_t NODE_REQUEST_TYPE = 0x0A;
/// VERIFIED: the vendor library sends it three times in a row, so a device that missed one still
/// answers.
static constexpr uint8_t NODE_REQUEST_REPEATS = 3;

/// VERIFIED: a device answers with its own address in the first four payload bytes, packed oddly:
/// the high five bits in the first byte, the low sixteen little-endian in the next two, and the two
/// remaining bits in the low end of the fourth. The first byte has room for a sixth bit, which has
/// never been seen set and is masked off.
///
/// LIBRARY: the other four bytes are, in the vendor's words, the component (byte 4), the power type
/// (byte 5) and the status version (byte 7, low five bits); bit 5 of byte 6 asks to be read again.
/// There is no device status in them: silence is the only status there is.
///
/// VERIFIED: an announcement is an answer and never sent unprompted, so on a bus where this hub is
/// the only node, nothing announces itself until the hub asks.
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
/// vendor calls the question ProdString, one of the NodeReq family (0x0E), told apart by its first
/// byte.
///
///     request   type 0x0E, 4 bytes: 0x30, string id little-endian, chunk index
///     answer    type 0x0C, the 4 byte header then up to four bytes of ASCII
///     absent    type 0x0D, the 4 byte header alone
///
/// The chunks are concatenated, and the string ends with a NUL or a short chunk.
static constexpr uint8_t STRING_REQUEST_TYPE = 0x0E;
static constexpr uint8_t STRING_INFORMATION_TYPE = 0x0C;
static constexpr uint8_t STRING_NOT_AVAILABLE_TYPE = 0x0D;
static constexpr uint8_t STRING_REQUEST_MARKER = 0x30;
static constexpr uint8_t STRING_HEADER_LENGTH = 4;
/// How much text one chunk carries, at most.
static constexpr uint8_t STRING_CHUNK_TEXT_LENGTH = 4;

/// How much of one string table entry a reader keeps, its terminator included. What a device keeps
/// there is a name, a label or a serial number, and the longest seen runs to about fifteen
/// characters ("Bat. temperature"); anything longer is cut short, and said to be.
static constexpr uint8_t STRING_BUFFER_LENGTH = 32;

/// Copy one chunk of a string answer into place and say whether the string ended there. Where the
/// text belongs is the chunk number the answer carries, not a counter of our own.
///
/// `data` is a whole string answer, header included. `out` is always left NUL terminated. Where
/// `truncated` is given it is set if a byte had to be dropped, so that a caller can say so rather
/// than report a cut name as though the device sent it that way.
inline bool take_string_chunk(const uint8_t *data, size_t size, char *out, uint8_t capacity,
                              bool *truncated = nullptr) {
  // A short answer is the last one: the device sends four bytes of text until it runs out.
  bool ended = size < STRING_HEADER_LENGTH + STRING_CHUNK_TEXT_LENGTH;
  size_t at = static_cast<size_t>(data[3]) * STRING_CHUNK_TEXT_LENGTH;
  for (size_t i = STRING_HEADER_LENGTH; i < size; i++) {
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
  return (static_cast<size_t>(chunk) + 1) * STRING_CHUNK_TEXT_LENGTH >= static_cast<size_t>(capacity) - 1;
}

/** One entry of a device's string table, read a chunk at a time into a buffer the caller owns.
 *
 * The answer echoes the request's header, so it says which entry and which chunk it carries, and
 * only the chunk asked for last is taken. Another node reading the same entry is answered on the
 * same bus: without the check its chunks would land in the middle of ours, and its last one would
 * end our read before our first had arrived.
 */
class MasterbusStringRead {
 public:
  /// What an answer did to the read.
  enum class Result : uint8_t {
    RESULT_NOT_OURS,  // another entry, another chunk, or not a string answer at all
    RESULT_MORE,      // a chunk was taken, and the next one is to be asked for
    RESULT_DONE,      // the string ended, or filled the buffer
    RESULT_REFUSED,   // the device holds no such entry
  };

  /// Start reading entry `id` into `out`, which holds `capacity` bytes and must outlive the read.
  void begin(uint16_t id, char *out, uint8_t capacity) {
    this->out_ = out;
    this->id_ = id;
    this->capacity_ = capacity;
    this->chunk_ = 0;
    this->truncated_ = false;
    out[0] = '\0';
  }

  /// Offer an answer of the string family, whoever it was meant for.
  Result take(uint8_t type, const uint8_t *data, size_t size) {
    if ((type != STRING_INFORMATION_TYPE && type != STRING_NOT_AVAILABLE_TYPE) || size < STRING_HEADER_LENGTH ||
        data[0] != STRING_REQUEST_MARKER || (data[1] | (data[2] << 8)) != this->id_ || data[3] != this->chunk_)
      return Result::RESULT_NOT_OURS;
    if (type == STRING_NOT_AVAILABLE_TYPE)
      return Result::RESULT_REFUSED;
    if (take_string_chunk(data, size, this->out_, this->capacity_, &this->truncated_))
      return Result::RESULT_DONE;
    if (string_buffer_full(this->chunk_, this->capacity_)) {
      this->truncated_ = true;
      return Result::RESULT_DONE;
    }
    this->chunk_++;
    return Result::RESULT_MORE;
  }

  uint16_t get_id() const { return this->id_; }
  /// The chunk to ask for next.
  uint8_t get_chunk() const { return this->chunk_; }
  uint8_t get_capacity() const { return this->capacity_; }
  /// Whether the text had to be cut to fit, so that it is said rather than passed off as what the
  /// device sent.
  bool is_truncated() const { return this->truncated_; }

 protected:
  char *out_{nullptr};
  uint16_t id_{0};
  uint8_t capacity_{0};
  uint8_t chunk_{0};
  bool truncated_{false};
};

// ---------------------------------------------------------------------------
// General counts
// ---------------------------------------------------------------------------

/// VERIFIED as to shape, LIBRARY as to the names: a device answers a two byte question with a 16 bit
/// number. The vendor calls the family GeneralCount; it shares the string table's message numbers
/// and is told apart by its first byte.
///
///     request   type 0x0E, 2 bytes: 0x08, which count
///     answer    type 0x0C, 4 bytes: the two byte header, then the number little-endian
///     refused   type 0x0D, 2 bytes: the header alone
///
/// The ones the walk asks on its own account:
///
///     08 02   monitoring groups      08 04   history groups
///     08 06   configuration groups   08 08   alarm groups
///     08 3F   eventables, how many event slots a device has
///
/// VERIFIED: a group count matches the groups the device goes on to answer for. A device that
/// refuses one is walked the older way, until a group goes unanswered.
static constexpr uint8_t GENERAL_COUNT_SELECTOR = 0x08;
static constexpr uint8_t GENERAL_COUNT_REQUEST_LENGTH = 2;
static constexpr uint8_t GENERAL_COUNT_INFORMATION_LENGTH = 4;
/// Every answer of this family - a count, a string's id, a version - carries its value after the
/// two byte header.
static constexpr uint8_t GENERAL_VALUE_OFFSET = 2;

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
/// How many processors' versions are read. The vendor's example device has one; past this many a
/// reader stops asking.
static constexpr uint8_t MAX_PROCESSORS = 8;

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
/// VERIFIED: the product name is the name the device was given at installation, and the article
/// number is the one on its label.
static constexpr uint8_t GENERAL_STRING_SELECTOR = 0x09;
static constexpr uint8_t GENERAL_STRING_INFORMATION_LENGTH = 4;
static constexpr uint8_t GENERAL_STRING_ARTICLE_NUMBER = 1;
static constexpr uint8_t GENERAL_STRING_SERIAL_NUMBER = 2;
static constexpr uint8_t GENERAL_STRING_PRODUCT_NAME = 3;
static constexpr uint8_t GENERAL_STRING_REVISION_CODE = 4;
static constexpr uint8_t GENERAL_STRING_COUNT = 4;

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
/// rest. A property a field does not have simply goes unanswered - a boolean field has no minimum.
/// LIBRARY: the vendor library sends the fourth byte, an index, only for the list properties.
static constexpr uint8_t PROPERTY_REQUEST_TYPE = 0x31;
static constexpr uint8_t PROPERTY_INFORMATION_TYPE = 0x11;

enum class MasterbusProperty : uint8_t {
  MASTERBUS_PROPERTY_DISPLAY_TYPE = 0x02,
  MASTERBUS_PROPERTY_MINIMUM = 0x06,
  MASTERBUS_PROPERTY_MAXIMUM = 0x07,
  MASTERBUS_PROPERTY_STEP = 0x08,
  MASTERBUS_PROPERTY_NAME_STRING = 0x28,
  MASTERBUS_PROPERTY_UNIT_STRING = 0x2C,
  // LIBRARY: the factory default, by elimination, as a 32 bit value.
  MASTERBUS_PROPERTY_FACTORY = 0x09,
  // VERIFIED: whether the field takes a write, and whether it is an event, each a 16 bit number
  // that is set or not. A setting takes a write and is no event.
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

/// VERIFIED: what the list properties hold.
///
///     0x26, index   the string id of that option of a radio or dropdown field (u16)
///     0x20, index   that entry of a device list: a device's address, packed in four bytes the way
///                   an announcement packs it
///     0x20, 0       for an event field, the device whose eventables it lists, packed the same way
///     0x0F, index   that entry of an event field: an eventable number on that device (one byte,
///                   then a byte the vendor library ignores)
///
/// The field's maximum (0x07) is how many entries there are. LIBRARY: 0x29 and 0x2A are longer forms
/// of the name 0x28 carries.
///
/// VERIFIED: a device's count 0x40 + n is the field number of its eventable n, which is what names
/// an eventable.
static constexpr uint8_t PROPERTY_INDEX_OFFSET = 3;
static constexpr uint8_t PROPERTY_VALUE_OFFSET = 4;
/// How long an answer is whose value is a 16 bit number, four bytes - a float or a packed address -
/// or the single byte of an event's field. A group answer is laid out the same way.
static constexpr uint8_t PROPERTY_NUMBER_ANSWER_LENGTH = PROPERTY_VALUE_OFFSET + 2;
static constexpr uint8_t PROPERTY_FLOAT_ANSWER_LENGTH = PROPERTY_VALUE_OFFSET + 4;
static constexpr uint8_t PROPERTY_BYTE_ANSWER_LENGTH = PROPERTY_VALUE_OFFSET + 1;

/// A device's answer to a yes-or-no property of a field, such as whether it takes a write, or that
/// it has not given one.
enum class MasterbusAnswer : uint8_t {
  MASTERBUS_ANSWER_UNKNOWN,
  MASTERBUS_ANSWER_NO,
  MASTERBUS_ANSWER_YES,
};

constexpr MasterbusAnswer masterbus_answer(bool yes) {
  return yes ? MasterbusAnswer::MASTERBUS_ANSWER_YES : MasterbusAnswer::MASTERBUS_ANSWER_NO;
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

/// VERIFIED: a monitoring group is walked the same way a field is, one selector at a time.
///
///     request   type 0x32, 3 bytes: selector, group number little-endian
///               type 0x32, 4 bytes: selector, group number, index - for the field list
///     answer    type 0x12, the header, the index, then the value
///
/// The field count arrives as a float, which is odd for a count but holds on every group seen.
static constexpr uint8_t GROUP_REQUEST_TYPE = 0x32;
static constexpr uint8_t GROUP_INFORMATION_TYPE = 0x12;

enum class MasterbusGroupSelector : uint8_t {
  MASTERBUS_GROUP_SELECTOR_FIELD_AT_INDEX = 0x03,
  MASTERBUS_GROUP_SELECTOR_FIELD_COUNT = 0x07,
  MASTERBUS_GROUP_SELECTOR_NAME_STRING = 0x28,
};

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
/// VERIFIED on a wire: monitoring's three, and the requests and answers of every tab's values and
/// group walks. No refusal of a tab message has been captured: a device that will not answer stays
/// silent.
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

// The table has to agree with the three message numbers named above.
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

/// Which tab a frame of one tab message belongs to, by the number in `part` - the request, the
/// answer or the refusal - and whether it is that message at all. The four tabs use the same
/// payload shapes, so the message number is the only thing that says which tab a frame is about:
/// taking one for another would publish a field from the wrong tab.
constexpr bool message_tab(uint8_t type, MasterbusTabMessage message, uint8_t MasterbusMessageNumbers::*part,
                           MasterbusTab &out) {
  for (MasterbusTab tab : MASTERBUS_TABS) {
    if (tab_message(tab, message).*part == type) {
      out = tab;
      return true;
    }
  }
  return false;
}

/// The tab of an answer carrying a value.
constexpr bool data_information_tab(uint8_t type, MasterbusTab &out) {
  return message_tab(type, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA, &MasterbusMessageNumbers::information, out);
}

/// The tab of an answer to a property question.
constexpr bool property_information_tab(uint8_t type, MasterbusTab &out) {
  return message_tab(type, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_PROPERTY, &MasterbusMessageNumbers::information,
                     out);
}

/// The tab of the request that reads a value - and, six bytes long, writes one.
constexpr bool data_request_tab(uint8_t type, MasterbusTab &out) {
  return message_tab(type, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA, &MasterbusMessageNumbers::request, out);
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

/// VERIFIED: how a device asks for a field to be displayed, read from property 0x02, and what decides
/// which ESPHome platform the field becomes. The numbering is the vendor's own. A checkbox is a
/// state and a button a momentary command, a distinction the vendor library does not expose.
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
};

}  // namespace esphome::masterbus
