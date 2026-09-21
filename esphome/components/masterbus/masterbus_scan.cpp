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

static const char *const TAG = "masterbus.scan";

/// A device that answers nothing at all should not hold the walk up forever.
static constexpr uint16_t MAX_FIELDS_PER_GROUP = 64;

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

void MasterbusScanner::start() {
  if (this->is_running())
    return;
  this->device_index_ = 0;
  this->tab_ = MasterbusTab::MASTERBUS_TAB_MONITORING;
  this->group_ = 0;
  this->field_index_ = 0;
  this->completed_ = 0;
  this->product_code_known_ = false;
  this->groups_known_ = false;
  this->phase_ = Phase::PHASE_PRODUCT_CODE;
  this->waiting_ = false;
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
    // No answer. For the two list walks that means the end of the list; for a field property it
    // means the field does not carry that one, which is normal - a boolean has no minimum.
    this->advance_(false);
  }
}

void MasterbusScanner::send_current_() {
  const uint32_t address = this->hub_->get_discovered_devices()[this->device_index_].address;
  switch (this->phase_) {
    case Phase::PHASE_PRODUCT_CODE:
      this->hub_->request_device_property(address, DEVICE_PROPERTY_PRODUCT_CODE);
      break;
    case Phase::PHASE_GROUP_COUNT:
      this->hub_->request_device_property(address, group_count_question(this->tab_));
      break;
    case Phase::PHASE_GROUP_FIELD_COUNT:
      this->hub_->request_group(address, MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_COUNT, this->group_,
                                this->tab_);
      break;
    case Phase::PHASE_GROUP_NAME_ID:
      this->hub_->request_group(address, MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_NAME_STRING, this->group_,
                                this->tab_);
      break;
    case Phase::PHASE_GROUP_NAME_TEXT:
    case Phase::PHASE_FIELD_NAME_TEXT:
      this->hub_->request_string(address, this->name_string_, this->chunk_);
      break;
    case Phase::PHASE_FIELD_NUMBER:
      this->hub_->request_group_index(address, this->group_, this->field_index_, this->tab_);
      break;
    case Phase::PHASE_FIELD_DISPLAY_TYPE:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_DISPLAY_TYPE, this->field_.param,
                                   this->tab_);
      break;
    case Phase::PHASE_FIELD_NAME_ID:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_NAME_STRING, this->field_.param,
                                   this->tab_);
      break;
    case Phase::PHASE_FIELD_UNIT_ID:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_UNIT_STRING, this->field_.param,
                                   this->tab_);
      break;
    case Phase::PHASE_FIELD_MINIMUM:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_MINIMUM, this->field_.param,
                                   this->tab_);
      break;
    case Phase::PHASE_FIELD_MAXIMUM:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_MAXIMUM, this->field_.param,
                                   this->tab_);
      break;
    case Phase::PHASE_FIELD_STEP:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_STEP, this->field_.param, this->tab_);
      break;
    case Phase::PHASE_FIELD_UNIT_TEXT:
      this->hub_->request_string(address, this->unit_string_, this->chunk_);
      break;
    case Phase::PHASE_IDLE:
      break;
  }
}

void MasterbusScanner::advance_(bool answered) {
  this->waiting_ = false;
  switch (this->phase_) {
    case Phase::PHASE_PRODUCT_CODE:
      // Asked once per device, and the answer is reported whether it came or not: a device that
      // refuses the question is still worth listing, and its fields are still worth walking.
      this->report_device_();
      this->phase_ = Phase::PHASE_GROUP_COUNT;
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
      this->chunk_ = 0;
      if (this->name_string_ != 0) {
        this->phase_ = Phase::PHASE_FIELD_NAME_TEXT;
        return;
      }
      // Asking for string zero would only burn the answer timeout, on every field that carries
      // no unit - and most of them do not.
      if (this->unit_string_ != 0) {
        this->phase_ = Phase::PHASE_FIELD_UNIT_TEXT;
        return;
      }
      this->finish_field_();
      return;

    case Phase::PHASE_FIELD_NAME_TEXT:
      if (!answered) {
        this->chunk_ = 0;
        this->phase_ = this->unit_string_ != 0 ? Phase::PHASE_FIELD_UNIT_TEXT : Phase::PHASE_IDLE;
        if (this->phase_ == Phase::PHASE_IDLE)
          this->finish_field_();
        return;
      }
      return;  // more chunks; on_frame decides when the string ended

    case Phase::PHASE_FIELD_UNIT_TEXT:
      if (!answered) {
        this->finish_field_();
        return;
      }
      return;

    case Phase::PHASE_IDLE:
      return;
  }
}

void MasterbusScanner::finish_field_() {
  this->report_field_();
  this->completed_++;
  this->field_index_++;
  this->chunk_ = 0;
  this->name_string_ = 0;
  this->unit_string_ = 0;
  this->waiting_ = false;
  // The group said how many fields it holds, so the last one is known without asking for one more.
  // The cap is for a count no device should give.
  if (this->field_index_ >= this->fields_in_group_ || this->field_index_ >= MAX_FIELDS_PER_GROUP) {
    this->next_group_();
    return;
  }
  this->phase_ = Phase::PHASE_FIELD_NUMBER;
}

void MasterbusScanner::next_group_() {
  this->group_++;
  this->group_reported_ = false;
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
    this->next_device_();
    return;
  }
  this->tab_ = static_cast<MasterbusTab>(static_cast<uint8_t>(this->tab_) + 1);
  this->group_ = 0;
  this->group_reported_ = false;
  this->field_index_ = 0;
  this->groups_known_ = false;
  this->phase_ = Phase::PHASE_GROUP_COUNT;
  this->waiting_ = false;
}

void MasterbusScanner::next_device_() {
  this->device_index_++;
  this->tab_ = MasterbusTab::MASTERBUS_TAB_MONITORING;
  this->group_ = 0;
  this->group_reported_ = false;
  this->field_index_ = 0;
  this->product_code_known_ = false;
  this->groups_known_ = false;
  this->phase_ = Phase::PHASE_PRODUCT_CODE;
  this->waiting_ = false;
}

bool MasterbusScanner::on_frame(uint8_t type, uint32_t address, const std::vector<uint8_t> &data) {
  if (!this->is_running() || !this->waiting_)
    return false;
  const auto &devices = this->hub_->get_discovered_devices();
  if (this->device_index_ >= devices.size() || address != devices[this->device_index_].address)
    return false;

  // A three byte header is followed by a tag and then the value; the four byte header used for a
  // list index carries the value alone.
  const auto value16 = [&data](size_t at) { return encode_uint16(data[at + 1], data[at]); };
  const auto value32 = [&data](size_t at) {
    const uint32_t bits = encode_uint32(data[at + 3], data[at + 2], data[at + 1], data[at]);
    float out;
    memcpy(&out, &bits, sizeof(out));
    return out;
  };

  switch (this->phase_) {
    case Phase::PHASE_PRODUCT_CODE:
    case Phase::PHASE_GROUP_COUNT: {
      const uint8_t question =
          this->phase_ == Phase::PHASE_PRODUCT_CODE ? DEVICE_PROPERTY_PRODUCT_CODE : group_count_question(this->tab_);
      if (!is_device_property_header(data.data(), data.size(), question))
        return false;
      if (type == STRING_NOT_AVAILABLE_TYPE) {
        // Equipment that does not carry the question says so, and the walk carries on without it.
        this->advance_(false);
        return true;
      }
      if (type != STRING_INFORMATION_TYPE || data.size() < DEVICE_PROPERTY_INFORMATION_LENGTH)
        return false;
      if (this->phase_ == Phase::PHASE_PRODUCT_CODE) {
        this->product_code_ = value16(2);
        this->product_code_known_ = true;
      } else {
        this->groups_in_tab_ = value16(2);
        this->groups_known_ = true;
      }
      break;
    }

    case Phase::PHASE_GROUP_FIELD_COUNT:
      if (type != masterbus_information_type(group_message(this->tab_)) || data.size() < 8)
        return false;
      // The count arrives as a float, which is odd for a count but consistent on every group seen.
      this->fields_in_group_ = static_cast<uint16_t>(value32(4));
      break;

    case Phase::PHASE_GROUP_NAME_ID:
      if (type != masterbus_information_type(group_message(this->tab_)) || data.size() < 6)
        return false;
      this->name_string_ = value16(4);
      this->group_named_ = this->name_string_ != 0;
      break;

    case Phase::PHASE_FIELD_NUMBER:
      if (type != masterbus_information_type(group_message(this->tab_)) || data.size() < 6)
        return false;
      this->field_ = {};
      this->field_.minimum = NAN;
      this->field_.maximum = NAN;
      this->field_.step = NAN;
      this->field_.group = this->group_;
      this->field_.tab = this->tab_;
      this->field_.param = value16(4);
      break;

    case Phase::PHASE_FIELD_DISPLAY_TYPE:
      if (type != masterbus_information_type(property_message(this->tab_)) || data.size() < 6)
        return false;
      this->field_.display_type = static_cast<MasterbusDisplayType>(value16(4));
      break;

    case Phase::PHASE_FIELD_NAME_ID:
      if (type != masterbus_information_type(property_message(this->tab_)) || data.size() < 6)
        return false;
      this->name_string_ = value16(4);
      this->field_.has_name = this->name_string_ != 0;
      break;

    case Phase::PHASE_FIELD_UNIT_ID:
      if (type != masterbus_information_type(property_message(this->tab_)) || data.size() < 6)
        return false;
      this->unit_string_ = value16(4);
      this->field_.has_unit = this->unit_string_ != 0;
      break;

    case Phase::PHASE_FIELD_MINIMUM:
    case Phase::PHASE_FIELD_MAXIMUM:
    case Phase::PHASE_FIELD_STEP: {
      if (type != masterbus_information_type(property_message(this->tab_)) || data.size() < 8)
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

    case Phase::PHASE_GROUP_NAME_TEXT:
    case Phase::PHASE_FIELD_NAME_TEXT:
    case Phase::PHASE_FIELD_UNIT_TEXT: {
      if (type == STRING_NOT_AVAILABLE_TYPE) {
        this->advance_(false);
        return true;
      }
      if (type != STRING_INFORMATION_TYPE || data.size() < STRING_CHUNK_LENGTH)
        return false;
      char *out = this->field_.unit;
      uint8_t capacity = SCAN_UNIT_LENGTH;
      uint16_t wanted = this->unit_string_;
      if (this->phase_ == Phase::PHASE_GROUP_NAME_TEXT) {
        out = this->group_name_;
        capacity = SCAN_NAME_LENGTH;
        wanted = this->name_string_;
      } else if (this->phase_ == Phase::PHASE_FIELD_NAME_TEXT) {
        out = this->field_.name;
        capacity = SCAN_NAME_LENGTH;
        wanted = this->name_string_;
      }
      // The answer echoes the request header, so it says which string and which chunk it carries.
      // Believing our own counter instead once put the second half of "Battery" over the first,
      // leaving "Batt" followed by whatever a stray chunk held.
      if (data[0] != STRING_REQUEST_MARKER || value16(1) != wanted)
        return false;
      const bool ended = take_string_chunk(data.data(), data.size(), out, capacity);
      this->chunk_ = data[3] + 1;
      this->waiting_ = false;
      if (ended)
        this->advance_(false);  // the string is complete, move on the same way a refusal would
      return true;
    }

    case Phase::PHASE_IDLE:
      return false;
  }

  this->advance_(true);
  return true;
}

/// Name one device and, where it answered for it, the product it says it is. The product code is
/// what tells a user which of the documented configurations is theirs, and the line is written for
/// every device - including one that describes no fields at all, which would otherwise be missing
/// from the configuration the converter builds.
void MasterbusScanner::report_device_() {
  const uint32_t address = this->hub_->get_discovered_devices()[this->device_index_].address;
  if (this->product_code_known_) {
    ESP_LOGI(TAG, "device device=0x%06" PRIX32 " product=%u", address, static_cast<unsigned>(this->product_code_));
  } else {
    ESP_LOGI(TAG, "device device=0x%06" PRIX32, address);
  }
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

  ESP_LOGI(TAG, "field device=0x%06" PRIX32 " tab=%u group=%u param=%u%s%s%s%s", address,
           static_cast<unsigned>(this->field_.tab), this->field_.group, this->field_.param, display, name, unit, range);
}

}  // namespace esphome::masterbus

#endif
