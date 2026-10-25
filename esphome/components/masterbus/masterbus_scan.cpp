#include "masterbus_scan.h"

#ifdef USE_MASTERBUS_SCAN

#include "masterbus.h"

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.scan");

/// A device that answers nothing at all should not hold the walk up forever.
static constexpr uint16_t MAX_FIELDS_PER_GROUP = 64;
/// How many entries of one list are asked for at most. A list ends where a device stops
/// answering, and its maximum says where that should be; this only bounds a device that does
/// neither.
static constexpr uint8_t MAX_LIST_ENTRIES = 64;
/// After this many fields in a row without a long name, a device is taken to carry none.
static constexpr uint8_t LONG_NAME_GIVE_UP = 3;

/// How long a string can be once quoted: every byte escaped as \\xNN, then the quotes and the
/// terminator.
static constexpr size_t quoted_length(size_t length) { return length * 4 + 3; }

/// Write a string as a logfmt reader expects it: always quoted, the quote and the backslash
/// escaped, and every byte outside printable ASCII as \xNN. The bus is documented to send ASCII
/// but nothing enforces it, and a raw byte would mangle a log viewer that carries JSON.
static void quote_string(const char *value, char *out, size_t capacity) {
  size_t at = 0;
  const auto put = [&](char character) {
    if (at + 1 < capacity)
      out[at++] = character;
  };
  put('"');
  for (const char *character = value; *character != '\0'; character++) {
    const uint8_t byte = static_cast<uint8_t>(*character);
    if (byte == '"' || byte == '\\') {
      put('\\');
      put(static_cast<char>(byte));
    } else if (byte < 0x20 || byte > 0x7E) {
      put('\\');
      put('x');
      put(format_hex_pretty_char(byte >> 4));
      put(format_hex_pretty_char(byte & 0x0F));
    } else {
      put(static_cast<char>(byte));
    }
  }
  put('"');
  out[at] = '\0';
}

/// The questions asked about every field, in the order the walk asks them, which is the order of
/// their phases from PHASE_FIELD_DISPLAY_TYPE on.
static constexpr MasterbusFieldQuestion FIELD_QUESTIONS[] PROGMEM = {
    {MasterbusProperty::MASTERBUS_PROPERTY_DISPLAY_TYPE, PROPERTY_NUMBER_ANSWER_LENGTH},
    {MasterbusProperty::MASTERBUS_PROPERTY_NAME_STRING, PROPERTY_NUMBER_ANSWER_LENGTH},
    {MasterbusProperty::MASTERBUS_PROPERTY_UNIT_STRING, PROPERTY_NUMBER_ANSWER_LENGTH},
    {MasterbusProperty::MASTERBUS_PROPERTY_MINIMUM, PROPERTY_FLOAT_ANSWER_LENGTH},
    {MasterbusProperty::MASTERBUS_PROPERTY_MAXIMUM, PROPERTY_FLOAT_ANSWER_LENGTH},
    {MasterbusProperty::MASTERBUS_PROPERTY_STEP, PROPERTY_FLOAT_ANSWER_LENGTH},
    {MasterbusProperty::MASTERBUS_PROPERTY_FACTORY, PROPERTY_FLOAT_ANSWER_LENGTH},
    {MasterbusProperty::MASTERBUS_PROPERTY_WRITEABLE, PROPERTY_NUMBER_ANSWER_LENGTH},
    {MasterbusProperty::MASTERBUS_PROPERTY_EVENTABLE, PROPERTY_NUMBER_ANSWER_LENGTH},
    {MasterbusProperty::MASTERBUS_PROPERTY_LONG_NAME_STRING, PROPERTY_NUMBER_ANSWER_LENGTH},
    {MasterbusProperty::MASTERBUS_PROPERTY_VERY_LONG_NAME_STRING, PROPERTY_NUMBER_ANSWER_LENGTH},
};

/// The counts the walk asks for on its own account, which the round of every count leaves out.
static bool asked_elsewhere(uint8_t question) {
  return question == GENERAL_COUNT_EVENTABLES || question == GENERAL_COUNT_MONITORING_GROUPS ||
         question == GENERAL_COUNT_HISTORY_GROUPS || question == GENERAL_COUNT_CONFIGURATION_GROUPS ||
         question == GENERAL_COUNT_ALARM_GROUPS;
}

void MasterbusScanner::start() {
  if (this->is_running())
    return;
  this->device_index_ = 0;
  this->completed_ = 0;
  this->next_device_();
  ESP_LOGI(TAG,
           "Walking %u devices across all four tabs for their fields. What follows is what each "
           "device answered; paste it into the converter on the documentation page to get a "
           "configuration out of it.",
           static_cast<unsigned>(this->hub_->get_discovered_devices().size()));
}

void MasterbusScanner::loop(uint32_t now) {
  if (!this->is_running())
    return;
  if (this->device_index_ >= this->hub_->get_discovered_devices().size()) {
    ESP_LOGI(TAG, "Walk finished, %u fields reported", static_cast<unsigned>(this->completed_));
    this->phase_ = Phase::PHASE_IDLE;
    return;
  }
  if (!this->waiting_) {
    this->send_current_();
    this->sent_at_ = now;
    this->waiting_ = true;
  } else if (now - this->sent_at_ >= SCAN_ANSWER_TIMEOUT_MS) {
    // No answer. For the list walks that means the end of the list; for a field property it means
    // the field does not carry that one, which is normal - a boolean has no minimum.
    this->advance_(false);
  }
}

bool MasterbusScanner::field_question(Phase phase, MasterbusFieldQuestion &out) {
  static_assert(static_cast<uint8_t>(Phase::PHASE_FIELD_VERY_LONG_NAME_ID) -
                        static_cast<uint8_t>(Phase::PHASE_FIELD_DISPLAY_TYPE) + 1 ==
                    sizeof(FIELD_QUESTIONS) / sizeof(FIELD_QUESTIONS[0]),
                "one question per field phase");
  if (phase < Phase::PHASE_FIELD_DISPLAY_TYPE || phase > Phase::PHASE_FIELD_VERY_LONG_NAME_ID)
    return false;
  progmem_memcpy(&out,
                 &FIELD_QUESTIONS[static_cast<uint8_t>(phase) - static_cast<uint8_t>(Phase::PHASE_FIELD_DISPLAY_TYPE)],
                 sizeof(out));
  return true;
}

void MasterbusScanner::send_current_() {
  const uint32_t address = this->hub_->get_discovered_devices()[this->device_index_].address;
  const uint16_t param = this->field_.param;
  MasterbusFieldQuestion question;
  if (field_question(this->phase_, question)) {
    this->hub_->request_property(address, question.property, param, this->tab_);
    return;
  }
  switch (this->phase_) {
    case Phase::PHASE_EVENTABLES:
      this->hub_->request_general_count(address, GENERAL_COUNT_EVENTABLES);
      break;
    case Phase::PHASE_DEVICE_STRING_ID:
      this->hub_->request_general_string(address, this->device_string_);
      break;
    case Phase::PHASE_DEVICE_STRING_TEXT:
    case Phase::PHASE_GROUP_NAME_TEXT:
    case Phase::PHASE_FIELD_NAME_TEXT:
    case Phase::PHASE_FIELD_UNIT_TEXT:
    case Phase::PHASE_FIELD_LONG_NAME_TEXT:
    case Phase::PHASE_FIELD_VERY_LONG_NAME_TEXT:
    case Phase::PHASE_OPTION_TEXT:
      this->hub_->request_string(address, this->string_.get_id(), this->string_.get_chunk());
      break;
    case Phase::PHASE_SOFTWARE_VERSION:
      this->hub_->request_software_version(address, this->processor_);
      break;
    case Phase::PHASE_GENERAL_COUNT:
      this->hub_->request_general_count(address, this->general_count_);
      break;
    case Phase::PHASE_GROUP_COUNT:
      this->hub_->request_general_count(address, group_count_question(this->tab_));
      break;
    case Phase::PHASE_GROUP_FIELD_COUNT:
      this->hub_->request_group(address, MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_COUNT, this->group_,
                                this->tab_);
      break;
    case Phase::PHASE_GROUP_NAME_ID:
      this->hub_->request_group(address, MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_NAME_STRING, this->group_,
                                this->tab_);
      break;
    case Phase::PHASE_FIELD_NUMBER:
      this->hub_->request_group_index(address, this->group_, this->field_index_, this->tab_);
      break;
    case Phase::PHASE_OPTION_ID:
      this->hub_->request_property_entry(address, MasterbusProperty::MASTERBUS_PROPERTY_OPTION_STRING, param,
                                         this->entry_, this->tab_);
      break;
    case Phase::PHASE_LIST_DEVICE:
      this->hub_->request_property_entry(address, MasterbusProperty::MASTERBUS_PROPERTY_LIST_DEVICE, param,
                                         this->entry_, this->tab_);
      break;
    case Phase::PHASE_EVENT_TARGET:
      this->hub_->request_property_entry(address, MasterbusProperty::MASTERBUS_PROPERTY_LIST_DEVICE, param, 0,
                                         this->tab_);
      break;
    case Phase::PHASE_EVENT_FIELD:
      this->hub_->request_property_entry(address, MasterbusProperty::MASTERBUS_PROPERTY_EVENT_FIELD, param,
                                         this->entry_, this->tab_);
      break;
    default:
      break;
  }
}

void MasterbusScanner::advance_(bool answered) {
  this->waiting_ = false;
  switch (this->phase_) {
    case Phase::PHASE_EVENTABLES:
      this->device_string_ = 0;
      this->device_strings_known_ = 0;
      this->next_device_string_();
      return;

    // The device's own strings, each a question for which table entry holds it and then the entry.
    // A device that carries none of them is still listed and walked, the way one with no
    // eventables is.
    case Phase::PHASE_DEVICE_STRING_ID:
      if (answered) {
        this->read_string_(Phase::PHASE_DEVICE_STRING_TEXT, this->name_string_,
                           this->device_strings_[this->device_string_ - 1], STRING_BUFFER_LENGTH,
                           LOG_STR_LITERAL("device string"));
        return;
      }
      this->next_device_string_();
      return;
    case Phase::PHASE_DEVICE_STRING_TEXT:
      this->next_device_string_();
      return;

    // One processor after another, until the device refuses the one past its last.
    case Phase::PHASE_SOFTWARE_VERSION:
      if (answered && ++this->processor_ < MAX_PROCESSORS)
        return;
      this->report_device_();
      this->general_count_ = 0;
      this->next_general_count_();
      return;
    case Phase::PHASE_GENERAL_COUNT:
      this->next_general_count_();
      return;

    case Phase::PHASE_GROUP_COUNT:
      // A tab the device says holds nothing is not walked at all. Where the question went
      // unanswered the walk falls back to finding the end of the list by asking past it.
      if (this->groups_known_ && this->groups_in_tab_ == 0) {
        this->next_tab_();
        return;
      }
      this->phase_ = Phase::PHASE_GROUP_FIELD_COUNT;
      return;

    case Phase::PHASE_GROUP_FIELD_COUNT:
      if (!answered) {
        // No such group, so this tab is done. A device with nothing on a tab refuses its very
        // first group, which is the same signal the group walk already uses for the end of a list.
        this->next_tab_();
        return;
      }
      this->field_index_ = 0;
      this->name_string_ = 0;
      this->group_name_[0] = '\0';
      // Cleared with the name itself: a group whose name question goes unanswered would otherwise
      // inherit the previous group's flag and report an empty name instead of no name at all.
      this->group_named_ = false;
      this->group_reported_ = false;
      this->phase_ = Phase::PHASE_GROUP_NAME_ID;
      return;

    case Phase::PHASE_GROUP_NAME_ID:
      // A group without a name is normal; its fields are still worth listing, if it has any.
      if (this->name_string_ != 0) {
        this->read_string_(Phase::PHASE_GROUP_NAME_TEXT, this->name_string_, this->group_name_, STRING_BUFFER_LENGTH,
                           LOG_STR_LITERAL("name"));
      } else if (this->fields_in_group_ == 0) {
        this->next_group_();
      } else {
        this->phase_ = Phase::PHASE_FIELD_NUMBER;
      }
      return;

    case Phase::PHASE_GROUP_NAME_TEXT:
      this->name_string_ = 0;
      // The count came from the device, so an empty group needs no question to find that out.
      if (this->fields_in_group_ == 0) {
        this->next_group_();
        return;
      }
      this->phase_ = Phase::PHASE_FIELD_NUMBER;
      return;

    case Phase::PHASE_FIELD_NUMBER:
      if (!answered) {
        // The device stopped short of the count it gave. Its word on the group is done with.
        this->next_group_();
        return;
      }
      this->phase_ = Phase::PHASE_FIELD_DISPLAY_TYPE;
      return;

    case Phase::PHASE_FIELD_DISPLAY_TYPE:
      this->phase_ = Phase::PHASE_FIELD_NAME_ID;
      return;
    case Phase::PHASE_FIELD_NAME_ID:
      this->phase_ = Phase::PHASE_FIELD_UNIT_ID;
      return;
    case Phase::PHASE_FIELD_UNIT_ID:
      this->phase_ = Phase::PHASE_FIELD_MINIMUM;
      return;
    case Phase::PHASE_FIELD_MINIMUM:
      this->phase_ = Phase::PHASE_FIELD_MAXIMUM;
      return;
    case Phase::PHASE_FIELD_MAXIMUM:
      this->phase_ = Phase::PHASE_FIELD_STEP;
      return;
    case Phase::PHASE_FIELD_STEP:
      this->phase_ = Phase::PHASE_FIELD_FACTORY;
      return;
    case Phase::PHASE_FIELD_FACTORY:
      this->phase_ = Phase::PHASE_FIELD_WRITEABLE;
      return;
    case Phase::PHASE_FIELD_WRITEABLE:
      this->phase_ = Phase::PHASE_FIELD_EVENTABLE;
      return;
    case Phase::PHASE_FIELD_EVENTABLE:
      if (this->long_names_silent_ < LONG_NAME_GIVE_UP) {
        this->phase_ = Phase::PHASE_FIELD_LONG_NAME_ID;
        return;
      }
      this->next_field_text_();
      return;
    case Phase::PHASE_FIELD_LONG_NAME_ID:
      if (!answered) {
        this->long_names_silent_++;
        this->next_field_text_();
        return;
      }
      this->long_names_silent_ = 0;
      if (this->long_name_string_ != 0) {
        this->phase_ = Phase::PHASE_FIELD_VERY_LONG_NAME_ID;
        return;
      }
      this->next_field_text_();
      return;
    // After the last question about the field its strings are read. Each ends with a refusal, a
    // short chunk or silence, and the next one starts.
    case Phase::PHASE_FIELD_VERY_LONG_NAME_ID:
    case Phase::PHASE_FIELD_NAME_TEXT:
    case Phase::PHASE_FIELD_UNIT_TEXT:
    case Phase::PHASE_FIELD_LONG_NAME_TEXT:
    case Phase::PHASE_FIELD_VERY_LONG_NAME_TEXT:
      this->next_field_text_();
      return;

    case Phase::PHASE_OPTION_ID:
      if (!answered) {
        this->next_field_();
        return;
      }
      if (this->option_string_ != 0) {
        this->read_string_(Phase::PHASE_OPTION_TEXT, this->option_string_, this->option_text_, STRING_BUFFER_LENGTH,
                           LOG_STR_LITERAL("option"));
        return;
      }
      this->report_entry_(0);
      this->next_entry_();
      return;
    case Phase::PHASE_OPTION_TEXT:
      this->report_entry_(0);
      this->next_entry_();
      return;
    case Phase::PHASE_LIST_DEVICE:
    case Phase::PHASE_EVENT_FIELD:
      if (!answered) {
        this->next_field_();
        return;
      }
      this->next_entry_();
      return;
    case Phase::PHASE_EVENT_TARGET:
      // Without its target an event's entries still say something, so they are asked either way.
      this->entry_ = 0;
      this->phase_ = Phase::PHASE_EVENT_FIELD;
      return;

    case Phase::PHASE_IDLE:
      return;
  }
}

void MasterbusScanner::next_device_string_() {
  this->device_string_++;
  if (this->device_string_ <= GENERAL_STRING_COUNT) {
    this->device_strings_[this->device_string_ - 1][0] = '\0';
    this->phase_ = Phase::PHASE_DEVICE_STRING_ID;
    return;
  }
  this->processor_ = 0;
  this->phase_ = Phase::PHASE_SOFTWARE_VERSION;
}

void MasterbusScanner::next_general_count_() {
  do {
    this->general_count_++;
  } while (this->general_count_ <= GENERAL_COUNT_LAST_QUESTION &&
           (!is_general_count_question(this->general_count_) || asked_elsewhere(this->general_count_)));
  if (this->general_count_ <= GENERAL_COUNT_LAST_QUESTION) {
    this->phase_ = Phase::PHASE_GENERAL_COUNT;
    return;
  }
  this->phase_ = Phase::PHASE_GROUP_COUNT;
}

void MasterbusScanner::next_field_text_() {
  this->waiting_ = false;
  struct FieldString {
    Phase phase;
    uint16_t id;
    char *out;
    uint8_t capacity;
    const char *what;
  };
  // The field's strings, in the order their phases are declared, after every question about the
  // field; each is read after the one before it. Asking for string zero would only burn the answer
  // timeout, on every field that carries no unit - and most of them do not.
  const FieldString strings[] = {
      {Phase::PHASE_FIELD_NAME_TEXT, this->name_string_, this->field_.name, STRING_BUFFER_LENGTH,
       LOG_STR_LITERAL("name")},
      {Phase::PHASE_FIELD_UNIT_TEXT, this->unit_string_, this->field_.unit, SCAN_UNIT_LENGTH, LOG_STR_LITERAL("unit")},
      {Phase::PHASE_FIELD_LONG_NAME_TEXT, this->long_name_string_, this->field_.long_name, STRING_BUFFER_LENGTH,
       LOG_STR_LITERAL("name")},
      {Phase::PHASE_FIELD_VERY_LONG_NAME_TEXT, this->very_long_name_string_, this->field_.very_long_name,
       STRING_BUFFER_LENGTH, LOG_STR_LITERAL("name")},
  };
  for (const FieldString &string : strings) {
    if (string.phase > this->phase_ && string.id != 0) {
      this->read_string_(string.phase, string.id, string.out, string.capacity, string.what);
      return;
    }
  }
  this->complete_field_();
}

void MasterbusScanner::read_string_(Phase phase, uint16_t id, char *out, uint8_t capacity, const char *what) {
  this->phase_ = phase;
  this->string_.begin(id, out, capacity);
  this->string_what_ = what;
}

void MasterbusScanner::complete_field_() {
  this->report_field_();
  this->entry_ = 0;
  if (this->field_.maximum == 0.0f) {
    this->next_field_();
    return;
  }
  switch (this->field_.display_type) {
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_RADIO:
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_DROPDOWN:
      this->phase_ = Phase::PHASE_OPTION_ID;
      return;
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_DEVICE_LIST:
      this->phase_ = Phase::PHASE_LIST_DEVICE;
      return;
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_EVENT:
      this->event_target_known_ = false;
      this->phase_ = Phase::PHASE_EVENT_TARGET;
      return;
    default:
      this->next_field_();
      return;
  }
}

void MasterbusScanner::next_entry_() {
  this->entry_++;
  this->option_string_ = 0;
  // The maximum is how many entries there are. A device answers past the end of a device list or an
  // event with filler - FF FF FF FF, a number one past its eventables - so the walk stops at the
  // maximum, as the vendor library does, and only a field without one is walked until the device
  // falls silent.
  uint8_t entries = MAX_LIST_ENTRIES;
  if (this->field_.maximum >= 0.0f && this->field_.maximum < MAX_LIST_ENTRIES)
    entries = static_cast<uint8_t>(this->field_.maximum);
  if (this->entry_ >= entries) {
    this->next_field_();
    return;
  }
  if (this->phase_ == Phase::PHASE_OPTION_TEXT)
    this->phase_ = Phase::PHASE_OPTION_ID;
}

void MasterbusScanner::next_field_() {
  this->completed_++;
  this->field_index_++;
  this->name_string_ = 0;
  this->unit_string_ = 0;
  this->long_name_string_ = 0;
  this->very_long_name_string_ = 0;
  this->option_string_ = 0;
  this->waiting_ = false;
  if (this->field_index_ >= MAX_FIELDS_PER_GROUP && this->field_index_ < this->fields_in_group_) {
    if (!this->cap_reported_) {
      this->cap_reported_ = true;
      ESP_LOGW(TAG, "Group %u of device 0x%06" PRIX32 " holds %u fields; only the first %u are reported", this->group_,
               this->hub_->get_discovered_devices()[this->device_index_].address,
               static_cast<unsigned>(this->fields_in_group_), static_cast<unsigned>(MAX_FIELDS_PER_GROUP));
    }
    this->next_group_();
    return;
  }
  // The group said how many fields it holds, so the last one is known without asking for one more.
  if (this->field_index_ >= this->fields_in_group_) {
    this->next_group_();
    return;
  }
  this->phase_ = Phase::PHASE_FIELD_NUMBER;
}

void MasterbusScanner::next_group_() {
  this->group_++;
  this->group_reported_ = false;
  this->cap_reported_ = false;
  this->field_index_ = 0;
  this->waiting_ = false;
  // Past the last group the device owned up to, so the tab is finished without asking again.
  if (this->groups_known_ && this->group_ >= this->groups_in_tab_) {
    this->next_tab_();
    return;
  }
  this->phase_ = Phase::PHASE_GROUP_FIELD_COUNT;
}

void MasterbusScanner::next_tab_() {
  if (this->tab_ == MasterbusTab::MASTERBUS_TAB_CONFIGURATION) {
    this->device_index_++;
    this->next_device_();
    return;
  }
  this->tab_ = static_cast<MasterbusTab>(static_cast<uint8_t>(this->tab_) + 1);
  this->group_ = 0;
  this->group_reported_ = false;
  this->field_index_ = 0;
  this->groups_known_ = false;
  this->cap_reported_ = false;
  this->phase_ = Phase::PHASE_GROUP_COUNT;
  this->waiting_ = false;
}

/// Set the walk up for the device at device_index_, beginning with what it says about itself.
void MasterbusScanner::next_device_() {
  this->tab_ = MasterbusTab::MASTERBUS_TAB_MONITORING;
  this->group_ = 0;
  this->group_reported_ = false;
  this->field_index_ = 0;
  this->eventables_known_ = false;
  this->device_strings_known_ = 0;
  this->processor_ = 0;
  this->long_names_silent_ = 0;
  this->groups_known_ = false;
  this->cap_reported_ = false;
  this->phase_ = Phase::PHASE_EVENTABLES;
  this->waiting_ = false;
}

bool MasterbusScanner::on_frame(uint8_t type, uint32_t address, const std::vector<uint8_t> &data) {
  if (!this->is_running() || !this->waiting_)
    return false;
  const auto &devices = this->hub_->get_discovered_devices();
  if (this->device_index_ >= devices.size() || address != devices[this->device_index_].address)
    return false;

  // A three byte header is followed by the index and then the value; the two byte header of a
  // count or a version carries the value alone.
  const auto value16 = [&data](size_t at) { return encode_uint16(data[at + 1], data[at]); };
  const auto raw32 = [&data](size_t at) { return encode_uint32(data[at + 3], data[at + 2], data[at + 1], data[at]); };
  const auto value32 = [&raw32](size_t at) {
    const uint32_t bits = raw32(at);
    float out;
    memcpy(&out, &bits, sizeof(out));
    return out;
  };
  // A group or property answer echoes the selector and the group or field number it was asked
  // for. A display panel walking the same device is answered in the same shape on the same bus, so
  // an answer that does not match the question is somebody else's.
  const auto is_answer = [&](uint8_t answer_type, uint8_t selector, uint16_t number, size_t length) {
    return type == answer_type && data.size() >= length && data[0] == selector && value16(1) == number;
  };
  const auto is_group = [&](MasterbusGroupSelector selector, size_t length) {
    return is_answer(this->group_answer_type_(), static_cast<uint8_t>(selector), this->group_, length);
  };
  const auto is_property = [&](MasterbusProperty property, size_t length) {
    return is_answer(this->property_answer_type_(), static_cast<uint8_t>(property), this->field_.param, length);
  };
  // A list property echoes the index as well, so a late answer to the previous entry is not taken
  // for this one.
  const auto is_entry = [&](MasterbusProperty property, uint8_t index, size_t length) {
    return is_property(property, length) && data[PROPERTY_INDEX_OFFSET] == index;
  };

  // A question about the field: the answer has to be to that property of that field.
  MasterbusFieldQuestion question;
  if (field_question(this->phase_, question) && !is_property(question.property, question.answer_length))
    return false;

  switch (this->phase_) {
    case Phase::PHASE_EVENTABLES:
    case Phase::PHASE_GENERAL_COUNT:
    case Phase::PHASE_GROUP_COUNT: {
      const uint8_t question = this->phase_ == Phase::PHASE_EVENTABLES      ? GENERAL_COUNT_EVENTABLES
                               : this->phase_ == Phase::PHASE_GENERAL_COUNT ? this->general_count_
                                                                            : group_count_question(this->tab_);
      if (!is_general_count_header(data.data(), data.size(), question))
        return false;
      if (type == STRING_NOT_AVAILABLE_TYPE) {
        // Equipment that does not carry the question says so, and the walk carries on without it.
        this->advance_(false);
        return true;
      }
      if (type != STRING_INFORMATION_TYPE || data.size() < GENERAL_COUNT_INFORMATION_LENGTH)
        return false;
      if (this->phase_ == Phase::PHASE_EVENTABLES) {
        this->eventables_ = value16(GENERAL_VALUE_OFFSET);
        this->eventables_known_ = true;
      } else if (this->phase_ == Phase::PHASE_GENERAL_COUNT) {
        ESP_LOGI(TAG, "count device=0x%06" PRIX32 " q=0x%02X value=%u", address, question,
                 static_cast<unsigned>(value16(GENERAL_VALUE_OFFSET)));
      } else {
        this->groups_in_tab_ = value16(GENERAL_VALUE_OFFSET);
        this->groups_known_ = true;
      }
      break;
    }

    case Phase::PHASE_DEVICE_STRING_ID:
      if (!is_general_string_header(data.data(), data.size(), this->device_string_))
        return false;
      if (type == STRING_NOT_AVAILABLE_TYPE) {
        this->advance_(false);
        return true;
      }
      if (type != STRING_INFORMATION_TYPE || data.size() < GENERAL_STRING_INFORMATION_LENGTH)
        return false;
      // Unlike a field's name, string 0 is a real entry here: the vendor's example device keeps its
      // serial number there.
      this->name_string_ = value16(GENERAL_VALUE_OFFSET);
      break;

    case Phase::PHASE_SOFTWARE_VERSION:
      if (!is_software_version_header(data.data(), data.size(), this->processor_))
        return false;
      if (type == STRING_NOT_AVAILABLE_TYPE) {
        this->advance_(false);
        return true;
      }
      if (type != STRING_INFORMATION_TYPE || data.size() < SOFTWARE_VERSION_INFORMATION_LENGTH)
        return false;
      this->software_versions_[this->processor_] = value16(GENERAL_VALUE_OFFSET);
      break;

    case Phase::PHASE_GROUP_FIELD_COUNT: {
      if (!is_group(MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_COUNT, PROPERTY_FLOAT_ANSWER_LENGTH))
        return false;
      // The count arrives as a float, which is odd for a count but consistent on every group seen.
      // One that cannot be a count is an empty group.
      const float count = value32(PROPERTY_VALUE_OFFSET);
      this->fields_in_group_ = count >= 0.0f && count < 65536.0f ? static_cast<uint16_t>(count) : 0;
      break;
    }

    case Phase::PHASE_GROUP_NAME_ID:
      if (!is_group(MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_NAME_STRING, PROPERTY_NUMBER_ANSWER_LENGTH))
        return false;
      this->name_string_ = value16(PROPERTY_VALUE_OFFSET);
      this->group_named_ = this->name_string_ != 0;
      break;

    case Phase::PHASE_FIELD_NUMBER:
      if (!is_group(MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_AT_INDEX, PROPERTY_NUMBER_ANSWER_LENGTH) ||
          data[3] != static_cast<uint8_t>(this->field_index_))
        return false;
      this->field_ = {};
      this->field_.minimum = NAN;
      this->field_.maximum = NAN;
      this->field_.step = NAN;
      this->field_.factory = NAN;
      this->field_.writeable = MasterbusAnswer::MASTERBUS_ANSWER_UNKNOWN;
      this->field_.eventable = MasterbusAnswer::MASTERBUS_ANSWER_UNKNOWN;
      this->field_.group = this->group_;
      this->field_.tab = this->tab_;
      this->field_.param = value16(PROPERTY_VALUE_OFFSET);
      break;

    case Phase::PHASE_FIELD_DISPLAY_TYPE:
      this->field_.display_type = static_cast<MasterbusDisplayType>(value16(PROPERTY_VALUE_OFFSET));
      break;
    case Phase::PHASE_FIELD_NAME_ID:
      this->name_string_ = value16(PROPERTY_VALUE_OFFSET);
      this->field_.has_name = this->name_string_ != 0;
      break;
    case Phase::PHASE_FIELD_UNIT_ID:
      this->unit_string_ = value16(PROPERTY_VALUE_OFFSET);
      this->field_.has_unit = this->unit_string_ != 0;
      break;
    case Phase::PHASE_FIELD_MINIMUM:
      this->field_.minimum = value32(PROPERTY_VALUE_OFFSET);
      break;
    case Phase::PHASE_FIELD_MAXIMUM:
      this->field_.maximum = value32(PROPERTY_VALUE_OFFSET);
      break;
    case Phase::PHASE_FIELD_STEP:
      this->field_.step = value32(PROPERTY_VALUE_OFFSET);
      break;
    case Phase::PHASE_FIELD_FACTORY:
      this->field_.factory = value32(PROPERTY_VALUE_OFFSET);
      break;
    case Phase::PHASE_FIELD_WRITEABLE:
      this->field_.writeable = masterbus_answer(value16(PROPERTY_VALUE_OFFSET) != 0);
      break;
    case Phase::PHASE_FIELD_EVENTABLE:
      this->field_.eventable = masterbus_answer(value16(PROPERTY_VALUE_OFFSET) != 0);
      break;
    // A long name that is the name itself has nothing new to read.
    case Phase::PHASE_FIELD_LONG_NAME_ID: {
      const uint16_t id = value16(PROPERTY_VALUE_OFFSET);
      this->long_name_string_ = id != this->name_string_ ? id : 0;
      break;
    }
    case Phase::PHASE_FIELD_VERY_LONG_NAME_ID: {
      const uint16_t id = value16(PROPERTY_VALUE_OFFSET);
      this->very_long_name_string_ = id != this->name_string_ && id != this->long_name_string_ ? id : 0;
      break;
    }

    case Phase::PHASE_OPTION_ID:
      if (!is_entry(MasterbusProperty::MASTERBUS_PROPERTY_OPTION_STRING, this->entry_, PROPERTY_NUMBER_ANSWER_LENGTH))
        return false;
      this->option_string_ = value16(PROPERTY_VALUE_OFFSET);
      break;
    // A device's address arrives packed the way an announcement packs it.
    case Phase::PHASE_LIST_DEVICE:
      if (!is_entry(MasterbusProperty::MASTERBUS_PROPERTY_LIST_DEVICE, this->entry_, PROPERTY_FLOAT_ANSWER_LENGTH))
        return false;
      this->report_entry_(decode_announced_address(data.data() + PROPERTY_VALUE_OFFSET));
      break;
    case Phase::PHASE_EVENT_TARGET:
      if (!is_entry(MasterbusProperty::MASTERBUS_PROPERTY_LIST_DEVICE, 0, PROPERTY_FLOAT_ANSWER_LENGTH))
        return false;
      this->event_target_ = decode_announced_address(data.data() + PROPERTY_VALUE_OFFSET);
      this->event_target_known_ = true;
      break;
    case Phase::PHASE_EVENT_FIELD:
      if (!is_entry(MasterbusProperty::MASTERBUS_PROPERTY_EVENT_FIELD, this->entry_, PROPERTY_BYTE_ANSWER_LENGTH))
        return false;
      this->report_entry_(data[PROPERTY_VALUE_OFFSET]);
      break;

    case Phase::PHASE_DEVICE_STRING_TEXT:
    case Phase::PHASE_GROUP_NAME_TEXT:
    case Phase::PHASE_FIELD_NAME_TEXT:
    case Phase::PHASE_FIELD_UNIT_TEXT:
    case Phase::PHASE_FIELD_LONG_NAME_TEXT:
    case Phase::PHASE_FIELD_VERY_LONG_NAME_TEXT:
    case Phase::PHASE_OPTION_TEXT: {
      const MasterbusStringRead::Result result = this->string_.take(type, data.data(), data.size());
      if (result == MasterbusStringRead::Result::RESULT_NOT_OURS)
        return false;
      // Handed over, even if only as a refusal: the device has a string under that question.
      if (this->phase_ == Phase::PHASE_DEVICE_STRING_TEXT)
        this->device_strings_known_ |= 1 << (this->device_string_ - 1);
      this->waiting_ = false;
      if (result == MasterbusStringRead::Result::RESULT_MORE)
        return true;
      if (result == MasterbusStringRead::Result::RESULT_DONE && this->string_.is_truncated()) {
        ESP_LOGW(TAG, "Device 0x%06" PRIX32 " sent a longer %s than %u characters; it is reported cut short",
                 devices[this->device_index_].address, this->string_what_,
                 static_cast<unsigned>(this->string_.get_capacity() - 1));
      }
      this->advance_(false);  // the string is complete or refused, and the walk moves on either way
      return true;
    }

    case Phase::PHASE_IDLE:
      return false;
  }

  this->advance_(true);
  return true;
}

// The key each of the device's own strings is reported under, in the vendor's numbering from 1.
PROGMEM_STRING_TABLE(MasterbusScanStringKeys, "article", "serial", "name", "revision");
static_assert(MasterbusScanStringKeys::COUNT == GENERAL_STRING_COUNT, "one key per device string");

/// Name one device: its class, which its address carries, and what else it answered for. The line
/// is written for every device - including one that describes no fields at all, which would
/// otherwise be missing from the configuration the converter builds.

void MasterbusScanner::report_device_() {
  const uint32_t address = this->hub_->get_discovered_devices()[this->device_index_].address;
  char quoted[quoted_length(STRING_BUFFER_LENGTH)];
  // Class, eventables, four quoted strings with their keys, and the versions.
  char line[24 + GENERAL_STRING_COUNT * (sizeof(quoted) + 12) + MAX_PROCESSORS * 12 + 16];
  size_t at = snprintf(line, sizeof(line), " class=%u", static_cast<unsigned>(device_class(address)));
  if (this->eventables_known_)
    at += snprintf(line + at, sizeof(line) - at, " eventables=%u", static_cast<unsigned>(this->eventables_));
  for (uint8_t i = 0; i < GENERAL_STRING_COUNT; i++) {
    if ((this->device_strings_known_ & (1 << i)) == 0)
      continue;
    quote_string(this->device_strings_[i], quoted, sizeof(quoted));
    at += snprintf(line + at, sizeof(line) - at, " %s=%s",
                   LOG_STR_ARG(MasterbusScanStringKeys::get_log_str(i, MasterbusScanStringKeys::LAST_INDEX)), quoted);
  }
  if (this->processor_ > 0) {
    // One version per processor, as the device numbers them: high byte, a dot, low byte.
    at += snprintf(line + at, sizeof(line) - at, " firmware=\"");
    for (uint8_t i = 0; i < this->processor_ && at < sizeof(line); i++) {
      at += snprintf(line + at, sizeof(line) - at, "%s%u.%u", i == 0 ? "" : " ",
                     static_cast<unsigned>(this->software_versions_[i] >> 8),
                     static_cast<unsigned>(this->software_versions_[i] & 0xFF));
    }
    if (at < sizeof(line))
      snprintf(line + at, sizeof(line) - at, "\"");
  }
  ESP_LOGI(TAG, "device device=0x%06" PRIX32 "%s", address, line);
}

/// Render one field as the configuration a user would write for it. The display type is what
/// picks the platform, which is the whole point of asking for it.
void MasterbusScanner::report_field_() {
  // A name and a unit get their own buffers, each sized for the string it holds once quoted.
  char quoted[quoted_length(STRING_BUFFER_LENGTH)];
  char quoted_unit[quoted_length(SCAN_UNIT_LENGTH)];
  const uint32_t address = this->hub_->get_discovered_devices()[this->device_index_].address;

  if (!this->group_reported_) {
    if (this->group_named_) {
      quote_string(this->group_name_, quoted, sizeof(quoted));
      ESP_LOGI(TAG, "group device=0x%06" PRIX32 " tab=%u group=%u name=%s", address,
               static_cast<unsigned>(this->field_.tab), this->group_, quoted);
    } else {
      ESP_LOGI(TAG, "group device=0x%06" PRIX32 " tab=%u group=%u", address, static_cast<unsigned>(this->field_.tab),
               this->group_);
    }
    this->group_reported_ = true;
  }

  // A key the device did not answer for is left out rather than given a value it never sent.
  char display[16] = "";
  if (this->field_.display_type != static_cast<MasterbusDisplayType>(0))
    snprintf(display, sizeof(display), " display=%u", static_cast<unsigned>(this->field_.display_type));

  char name[sizeof(quoted) + 8] = "";
  if (this->field_.has_name) {
    quote_string(this->field_.name, quoted, sizeof(quoted));
    snprintf(name, sizeof(name), " name=%s", quoted);
  }

  char unit[sizeof(quoted_unit) + 8] = "";
  if (this->field_.has_unit) {
    quote_string(this->field_.unit, quoted_unit, sizeof(quoted_unit));
    snprintf(unit, sizeof(unit), " unit=%s", quoted_unit);
  }

  char range[64] = "";
  size_t at = 0;
  if (!std::isnan(this->field_.minimum))
    at += snprintf(range + at, sizeof(range) - at, " min=%g", this->field_.minimum);
  if (!std::isnan(this->field_.maximum))
    at += snprintf(range + at, sizeof(range) - at, " max=%g", this->field_.maximum);
  if (!std::isnan(this->field_.step))
    snprintf(range + at, sizeof(range) - at, " step=%g", this->field_.step);

  char extra[64] = "";
  at = 0;
  if (!std::isnan(this->field_.factory))
    at += snprintf(extra + at, sizeof(extra) - at, " factory=%g", this->field_.factory);
  if (this->field_.writeable != MasterbusAnswer::MASTERBUS_ANSWER_UNKNOWN) {
    at += snprintf(extra + at, sizeof(extra) - at, " writable=%d",
                   this->field_.writeable == MasterbusAnswer::MASTERBUS_ANSWER_YES ? 1 : 0);
  }
  if (this->field_.eventable != MasterbusAnswer::MASTERBUS_ANSWER_UNKNOWN) {
    snprintf(extra + at, sizeof(extra) - at, " eventable=%d",
             this->field_.eventable == MasterbusAnswer::MASTERBUS_ANSWER_YES ? 1 : 0);
  }

  // The longer names, where the device has one that says more than the name does.
  char longer[2 * (sizeof(quoted) + 18)] = "";
  at = 0;
  if (this->field_.long_name[0] != '\0' && strcmp(this->field_.long_name, this->field_.name) != 0) {
    quote_string(this->field_.long_name, quoted, sizeof(quoted));
    at += snprintf(longer + at, sizeof(longer) - at, " long_name=%s", quoted);
  }
  if (this->field_.very_long_name[0] != '\0' && strcmp(this->field_.very_long_name, this->field_.name) != 0 &&
      strcmp(this->field_.very_long_name, this->field_.long_name) != 0) {
    quote_string(this->field_.very_long_name, quoted, sizeof(quoted));
    snprintf(longer + at, sizeof(longer) - at, " very_long_name=%s", quoted);
  }

  ESP_LOGI(TAG, "field device=0x%06" PRIX32 " tab=%u group=%u param=%u%s%s%s%s%s%s", address,
           static_cast<unsigned>(this->field_.tab), this->field_.group, this->field_.param, display, name, unit, range,
           extra, longer);
}

/// One entry of the list field just reported: an option and its name, a device on a device list,
/// or one of the eventables of the device an event commands.
void MasterbusScanner::report_entry_(uint32_t value) {
  const uint32_t address = this->hub_->get_discovered_devices()[this->device_index_].address;
  char what[quoted_length(sizeof(this->option_text_)) + 29] = "";
  switch (this->field_.display_type) {
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_DEVICE_LIST:
      snprintf(what, sizeof(what), " entry=0x%06" PRIX32, value);
      break;
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_EVENT:
      if (this->event_target_known_) {
        snprintf(what, sizeof(what), " target=0x%06" PRIX32 " eventable=%u", this->event_target_,
                 static_cast<unsigned>(value));
      } else {
        snprintf(what, sizeof(what), " eventable=%u", static_cast<unsigned>(value));
      }
      break;
    default:
      if (this->option_string_ != 0) {
        char quoted[quoted_length(sizeof(this->option_text_))];
        quote_string(this->option_text_, quoted, sizeof(quoted));
        snprintf(what, sizeof(what), " name=%s", quoted);
      }
      break;
  }
  ESP_LOGI(TAG, "option device=0x%06" PRIX32 " tab=%u param=%u index=%u%s", address,
           static_cast<unsigned>(this->field_.tab), this->field_.param, static_cast<unsigned>(this->entry_), what);
}

}  // namespace esphome::masterbus

#endif
