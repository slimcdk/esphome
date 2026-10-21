#include "masterbus_scan.h"

#ifdef USE_MASTERBUS_SCAN

#include "masterbus.h"

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

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
/// The device's own strings, in the vendor's numbering, and the key each is reported under.
static constexpr uint8_t DEVICE_STRING_COUNT = 4;

/// Write a string as a logfmt reader expects it: always quoted, the quote and the backslash
/// escaped, and every byte outside printable ASCII as \xNN. The bus is documented to send ASCII
/// but nothing enforces it, and a raw byte would mangle a log viewer that carries JSON.
static void quote_string(const char *value, char *out, size_t capacity) {
  static const char HEX[] = "0123456789ABCDEF";
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
      put(HEX[byte >> 4]);
      put(HEX[byte & 0x0F]);
    } else {
      put(static_cast<char>(byte));
    }
  }
  put('"');
  out[at] = '\0';
}

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

void MasterbusScanner::send_current_() {
  const uint32_t address = this->hub_->get_discovered_devices()[this->device_index_].address;
  const uint16_t param = this->field_.param;
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
      this->hub_->request_string(address, this->name_string_, this->chunk_);
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
    case Phase::PHASE_FIELD_DISPLAY_TYPE:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_DISPLAY_TYPE, param, this->tab_);
      break;
    case Phase::PHASE_FIELD_NAME_ID:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_NAME_STRING, param, this->tab_);
      break;
    case Phase::PHASE_FIELD_UNIT_ID:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_UNIT_STRING, param, this->tab_);
      break;
    case Phase::PHASE_FIELD_MINIMUM:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_MINIMUM, param, this->tab_);
      break;
    case Phase::PHASE_FIELD_MAXIMUM:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_MAXIMUM, param, this->tab_);
      break;
    case Phase::PHASE_FIELD_STEP:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_STEP, param, this->tab_);
      break;
    case Phase::PHASE_FIELD_FACTORY:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_FACTORY, param, this->tab_);
      break;
    case Phase::PHASE_FIELD_WRITEABLE:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_WRITEABLE, param, this->tab_);
      break;
    case Phase::PHASE_FIELD_EVENTABLE:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_EVENTABLE, param, this->tab_);
      break;
    case Phase::PHASE_FIELD_LONG_NAME_ID:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_LONG_NAME_STRING, param, this->tab_);
      break;
    case Phase::PHASE_FIELD_VERY_LONG_NAME_ID:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_VERY_LONG_NAME_STRING, param,
                                   this->tab_);
      break;
    case Phase::PHASE_FIELD_UNIT_TEXT:
      this->hub_->request_string(address, this->unit_string_, this->chunk_);
      break;
    case Phase::PHASE_FIELD_LONG_NAME_TEXT:
      this->hub_->request_string(address, this->long_name_string_, this->chunk_);
      break;
    case Phase::PHASE_FIELD_VERY_LONG_NAME_TEXT:
      this->hub_->request_string(address, this->very_long_name_string_, this->chunk_);
      break;
    case Phase::PHASE_OPTION_ID:
      this->hub_->request_property_entry(address, MasterbusProperty::MASTERBUS_PROPERTY_OPTION_STRING, param,
                                         this->entry_, this->tab_);
      break;
    case Phase::PHASE_OPTION_TEXT:
      this->hub_->request_string(address, this->option_string_, this->chunk_);
      break;
    case Phase::PHASE_LIST_DEVICE:
      this->hub_->request_property_entry(address, MasterbusProperty::MASTERBUS_PROPERTY_LIST_DEVICE, param,
                                         this->entry_, this->tab_);
      break;
    case Phase::PHASE_EVENT_SOURCE:
      this->hub_->request_property_entry(address, MasterbusProperty::MASTERBUS_PROPERTY_LIST_DEVICE, param, 0,
                                         this->tab_);
      break;
    case Phase::PHASE_EVENT_FIELD:
      this->hub_->request_property_entry(address, MasterbusProperty::MASTERBUS_PROPERTY_EVENT_FIELD, param,
                                         this->entry_, this->tab_);
      break;
    case Phase::PHASE_IDLE:
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
        this->chunk_ = 0;
        this->phase_ = Phase::PHASE_DEVICE_STRING_TEXT;
        return;
      }
      this->next_device_string_();
      return;
    case Phase::PHASE_DEVICE_STRING_TEXT:
      if (answered)
        return;  // more chunks; on_frame decides when the string ended
      this->chunk_ = 0;
      this->name_string_ = 0;
      this->next_device_string_();
      return;

    // One processor after another, until the device refuses the one past its last.
    case Phase::PHASE_SOFTWARE_VERSION:
      if (answered && ++this->processor_ < SCAN_MAX_PROCESSORS)
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
      this->chunk_ = 0;
      this->name_string_ = 0;
      this->group_name_[0] = '\0';
      // Cleared with the name itself: a group whose name question goes unanswered would otherwise
      // inherit the previous group's flag and report an empty name instead of no name at all.
      this->group_named_ = false;
      this->group_reported_ = false;
      this->phase_ = Phase::PHASE_GROUP_NAME_ID;
      return;

    case Phase::PHASE_GROUP_NAME_ID:
      this->chunk_ = 0;
      // A group without a name is normal; its fields are still worth listing.
      this->phase_ = this->name_string_ != 0 ? Phase::PHASE_GROUP_NAME_TEXT : Phase::PHASE_FIELD_NUMBER;
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
    case Phase::PHASE_FIELD_VERY_LONG_NAME_ID:
      this->next_field_text_();
      return;

    // Each string ends with a refusal, a short chunk or silence, and the next one starts.
    case Phase::PHASE_FIELD_NAME_TEXT:
    case Phase::PHASE_FIELD_UNIT_TEXT:
    case Phase::PHASE_FIELD_LONG_NAME_TEXT:
    case Phase::PHASE_FIELD_VERY_LONG_NAME_TEXT:
      if (answered)
        return;  // more chunks; on_frame decides when the string ended
      if (this->phase_ == Phase::PHASE_FIELD_NAME_TEXT) {
        this->name_string_ = 0;
      } else if (this->phase_ == Phase::PHASE_FIELD_UNIT_TEXT) {
        this->unit_string_ = 0;
      } else if (this->phase_ == Phase::PHASE_FIELD_LONG_NAME_TEXT) {
        this->long_name_string_ = 0;
      } else {
        this->very_long_name_string_ = 0;
      }
      this->next_field_text_();
      return;

    case Phase::PHASE_OPTION_ID:
      if (!answered) {
        this->next_field_();
        return;
      }
      if (this->option_string_ != 0) {
        this->chunk_ = 0;
        this->option_text_[0] = '\0';
        this->phase_ = Phase::PHASE_OPTION_TEXT;
        return;
      }
      this->report_entry_(0);
      this->next_entry_();
      return;
    case Phase::PHASE_OPTION_TEXT:
      if (answered)
        return;
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
    case Phase::PHASE_EVENT_SOURCE:
      // Without its source an event's fields still say something, so they are asked either way.
      this->entry_ = 0;
      this->phase_ = Phase::PHASE_EVENT_FIELD;
      return;

    case Phase::PHASE_IDLE:
      return;
  }
}

void MasterbusScanner::next_device_string_() {
  this->device_string_++;
  if (this->device_string_ <= DEVICE_STRING_COUNT) {
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
  this->chunk_ = 0;
  this->waiting_ = false;
  // Asking for string zero would only burn the answer timeout, on every field that carries no
  // unit - and most of them do not.
  if (this->name_string_ != 0) {
    this->phase_ = Phase::PHASE_FIELD_NAME_TEXT;
  } else if (this->unit_string_ != 0) {
    this->phase_ = Phase::PHASE_FIELD_UNIT_TEXT;
  } else if (this->long_name_string_ != 0) {
    this->phase_ = Phase::PHASE_FIELD_LONG_NAME_TEXT;
  } else if (this->very_long_name_string_ != 0) {
    this->phase_ = Phase::PHASE_FIELD_VERY_LONG_NAME_TEXT;
  } else {
    this->complete_field_();
  }
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
      this->event_source_known_ = false;
      this->phase_ = Phase::PHASE_EVENT_SOURCE;
      return;
    default:
      this->next_field_();
      return;
  }
}

void MasterbusScanner::next_entry_() {
  this->entry_++;
  this->option_string_ = 0;
  // The maximum is how many entries there are: 102 of 102 option lists on the reference bus held
  // exactly that many. A device answers past the end of a device list or an event with filler -
  // FF FF FF FF, a number one past its eventables - so the walk stops at the maximum, as the vendor
  // library does, and only a field without one is walked until the device falls silent.
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
  this->chunk_ = 0;
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
  // An answer to a list property echoes the selector and the index it was asked for, so a late
  // answer to the previous entry is not taken for this one.
  const auto is_entry = [&](MasterbusProperty property, uint8_t index, size_t length) {
    return type == this->property_answer_type_() && data.size() >= length &&
           data[0] == static_cast<uint8_t>(property) && data[PROPERTY_INDEX_OFFSET] == index;
  };

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
        this->eventables_ = value16(2);
        this->eventables_known_ = true;
      } else if (this->phase_ == Phase::PHASE_GENERAL_COUNT) {
        ESP_LOGI(TAG, "count device=0x%06" PRIX32 " q=0x%02X value=%u", address, question,
                 static_cast<unsigned>(value16(2)));
      } else {
        this->groups_in_tab_ = value16(2);
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
      this->name_string_ = value16(2);
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
      this->software_versions_[this->processor_] = value16(2);
      break;

    case Phase::PHASE_GROUP_FIELD_COUNT:
      if (type != this->group_answer_type_() || data.size() < 8)
        return false;
      // The count arrives as a float, which is odd for a count but consistent on every group seen.
      this->fields_in_group_ = static_cast<uint16_t>(value32(4));
      break;

    case Phase::PHASE_GROUP_NAME_ID:
      if (type != this->group_answer_type_() || data.size() < 6)
        return false;
      this->name_string_ = value16(4);
      this->group_named_ = this->name_string_ != 0;
      break;

    case Phase::PHASE_FIELD_NUMBER:
      if (type != this->group_answer_type_() || data.size() < 6)
        return false;
      this->field_ = {};
      this->field_.minimum = NAN;
      this->field_.maximum = NAN;
      this->field_.step = NAN;
      this->field_.factory = NAN;
      this->field_.writable = -1;
      this->field_.eventable = -1;
      this->field_.group = this->group_;
      this->field_.tab = this->tab_;
      this->field_.param = value16(4);
      break;

    case Phase::PHASE_FIELD_DISPLAY_TYPE:
      if (type != this->property_answer_type_() || data.size() < 6)
        return false;
      this->field_.display_type = static_cast<MasterbusDisplayType>(value16(4));
      break;

    case Phase::PHASE_FIELD_NAME_ID:
      if (type != this->property_answer_type_() || data.size() < 6)
        return false;
      this->name_string_ = value16(4);
      this->field_.has_name = this->name_string_ != 0;
      break;

    case Phase::PHASE_FIELD_UNIT_ID:
      if (type != this->property_answer_type_() || data.size() < 6)
        return false;
      this->unit_string_ = value16(4);
      this->field_.has_unit = this->unit_string_ != 0;
      break;

    case Phase::PHASE_FIELD_MINIMUM:
    case Phase::PHASE_FIELD_MAXIMUM:
    case Phase::PHASE_FIELD_STEP: {
      if (type != this->property_answer_type_() || data.size() < 8)
        return false;
      const float value = value32(4);
      if (this->phase_ == Phase::PHASE_FIELD_MINIMUM) {
        this->field_.minimum = value;
      } else if (this->phase_ == Phase::PHASE_FIELD_MAXIMUM) {
        this->field_.maximum = value;
      } else {
        this->field_.step = value;
      }
      break;
    }

    // The selector is checked as well as the length: these follow each other, and a late answer to
    // one must not be taken for the next.
    case Phase::PHASE_FIELD_FACTORY:
      if (type != this->property_answer_type_() || data.size() < 8 ||
          data[0] != static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_FACTORY))
        return false;
      this->field_.factory = value32(4);
      break;
    case Phase::PHASE_FIELD_WRITEABLE:
    case Phase::PHASE_FIELD_EVENTABLE: {
      const bool first = this->phase_ == Phase::PHASE_FIELD_WRITEABLE;
      const auto selector = static_cast<uint8_t>(first ? MasterbusProperty::MASTERBUS_PROPERTY_WRITEABLE
                                                       : MasterbusProperty::MASTERBUS_PROPERTY_EVENTABLE);
      if (type != this->property_answer_type_() || data.size() < 6 || data[0] != selector)
        return false;
      (first ? this->field_.writable : this->field_.eventable) = value16(4) != 0 ? 1 : 0;
      break;
    }
    case Phase::PHASE_FIELD_LONG_NAME_ID:
    case Phase::PHASE_FIELD_VERY_LONG_NAME_ID: {
      const bool first = this->phase_ == Phase::PHASE_FIELD_LONG_NAME_ID;
      const auto selector = static_cast<uint8_t>(first ? MasterbusProperty::MASTERBUS_PROPERTY_LONG_NAME_STRING
                                                       : MasterbusProperty::MASTERBUS_PROPERTY_VERY_LONG_NAME_STRING);
      if (type != this->property_answer_type_() || data.size() < 6 || data[0] != selector)
        return false;
      uint16_t id = value16(4);
      // A long name that is the name itself has nothing new to read.
      if (id == this->name_string_ || (!first && id == this->long_name_string_))
        id = 0;
      (first ? this->long_name_string_ : this->very_long_name_string_) = id;
      break;
    }

    case Phase::PHASE_OPTION_ID:
      if (!is_entry(MasterbusProperty::MASTERBUS_PROPERTY_OPTION_STRING, this->entry_, 6))
        return false;
      this->option_string_ = value16(PROPERTY_VALUE_OFFSET);
      break;
    // A device's address arrives packed the way an announcement packs it.
    case Phase::PHASE_LIST_DEVICE:
      if (!is_entry(MasterbusProperty::MASTERBUS_PROPERTY_LIST_DEVICE, this->entry_, 8))
        return false;
      this->report_entry_(decode_announced_address(data.data() + PROPERTY_VALUE_OFFSET));
      break;
    case Phase::PHASE_EVENT_SOURCE:
      if (!is_entry(MasterbusProperty::MASTERBUS_PROPERTY_LIST_DEVICE, 0, 8))
        return false;
      this->event_source_ = decode_announced_address(data.data() + PROPERTY_VALUE_OFFSET);
      this->event_source_known_ = true;
      break;
    case Phase::PHASE_EVENT_FIELD:
      if (!is_entry(MasterbusProperty::MASTERBUS_PROPERTY_EVENT_FIELD, this->entry_, 5))
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
      if (type == STRING_NOT_AVAILABLE_TYPE) {
        this->advance_(false);
        return true;
      }
      if (type != STRING_INFORMATION_TYPE || data.size() < STRING_CHUNK_LENGTH)
        return false;
      char *out = this->field_.name;
      uint8_t capacity = SCAN_NAME_LENGTH;
      uint16_t wanted = this->name_string_;
      const char *what = LOG_STR_LITERAL("name");
      switch (this->phase_) {
        case Phase::PHASE_DEVICE_STRING_TEXT:
          out = this->device_strings_[this->device_string_ - 1];
          this->device_strings_known_ |= 1 << (this->device_string_ - 1);
          what = LOG_STR_LITERAL("device string");
          break;
        case Phase::PHASE_GROUP_NAME_TEXT:
          out = this->group_name_;
          break;
        case Phase::PHASE_FIELD_UNIT_TEXT:
          out = this->field_.unit;
          capacity = SCAN_UNIT_LENGTH;
          wanted = this->unit_string_;
          what = LOG_STR_LITERAL("unit");
          break;
        case Phase::PHASE_FIELD_LONG_NAME_TEXT:
          out = this->field_.long_name;
          wanted = this->long_name_string_;
          break;
        case Phase::PHASE_FIELD_VERY_LONG_NAME_TEXT:
          out = this->field_.very_long_name;
          wanted = this->very_long_name_string_;
          break;
        case Phase::PHASE_OPTION_TEXT:
          out = this->option_text_;
          wanted = this->option_string_;
          what = LOG_STR_LITERAL("option");
          break;
        default:
          break;
      }
      // The answer echoes the request header, so it says which string and which chunk it carries.
      // Believing our own counter instead once put the second half of "Battery" over the first,
      // leaving "Batt" followed by whatever a stray chunk held.
      if (data[0] != STRING_REQUEST_MARKER || value16(1) != wanted)
        return false;
      if (data[3] == 0)
        this->string_truncated_ = false;
      const bool ended = take_string_chunk(data.data(), data.size(), out, capacity, &this->string_truncated_);
      this->chunk_ = data[3] + 1;
      this->waiting_ = false;
      if (ended) {
        if (this->string_truncated_) {
          ESP_LOGW(TAG, "Device 0x%06" PRIX32 " sent a longer %s than %u characters; it is reported cut short",
                   devices[this->device_index_].address, what, static_cast<unsigned>(capacity - 1));
        }
        this->advance_(false);  // the string is complete, move on the same way a refusal would
      }
      return true;
    }

    case Phase::PHASE_IDLE:
      return false;
  }

  this->advance_(true);
  return true;
}

/// Name one device: its class, which its address carries, and what else it answered for. The line
/// is written for every device - including one that describes no fields at all, which would
/// otherwise be missing from the configuration the converter builds.
void MasterbusScanner::report_device_() {
  static constexpr const char *STRING_KEYS[DEVICE_STRING_COUNT] = {"article", "serial", "name", "revision"};
  const uint32_t address = this->hub_->get_discovered_devices()[this->device_index_].address;
  char quoted[SCAN_NAME_LENGTH * 4 + 3];
  // Class, eventables, four quoted strings with their keys, and the versions.
  char line[24 + DEVICE_STRING_COUNT * (sizeof(quoted) + 12) + SCAN_MAX_PROCESSORS * 12 + 16];
  size_t at = snprintf(line, sizeof(line), " class=%u", static_cast<unsigned>(device_class(address)));
  if (this->eventables_known_)
    at += snprintf(line + at, sizeof(line) - at, " eventables=%u", static_cast<unsigned>(this->eventables_));
  for (uint8_t i = 0; i < DEVICE_STRING_COUNT; i++) {
    if ((this->device_strings_known_ & (1 << i)) == 0)
      continue;
    quote_string(this->device_strings_[i], quoted, sizeof(quoted));
    at += snprintf(line + at, sizeof(line) - at, " %s=%s", STRING_KEYS[i], quoted);
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
  // Quoted strings can grow fourfold, if every byte has to be escaped. A name and a unit get
  // their own buffers so each one's bound is the bound of the string it holds.
  char quoted[SCAN_NAME_LENGTH * 4 + 3];
  char quoted_unit[SCAN_UNIT_LENGTH * 4 + 3];
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
  if (this->field_.writable >= 0)
    at += snprintf(extra + at, sizeof(extra) - at, " writable=%d", this->field_.writable);
  if (this->field_.eventable >= 0)
    snprintf(extra + at, sizeof(extra) - at, " eventable=%d", this->field_.eventable);

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
  char what[sizeof(this->option_text_) * 4 + 32] = "";
  switch (this->field_.display_type) {
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_DEVICE_LIST:
      snprintf(what, sizeof(what), " entry=0x%06" PRIX32, value);
      break;
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_EVENT:
      if (this->event_source_known_) {
        snprintf(what, sizeof(what), " source=0x%06" PRIX32 " eventable=%u", this->event_source_,
                 static_cast<unsigned>(value));
      } else {
        snprintf(what, sizeof(what), " eventable=%u", static_cast<unsigned>(value));
      }
      break;
    default:
      if (this->option_string_ != 0) {
        char quoted[sizeof(this->option_text_) * 4 + 3];
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
