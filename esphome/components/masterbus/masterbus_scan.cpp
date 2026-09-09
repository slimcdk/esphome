#include "masterbus_scan.h"

#ifdef USE_MASTERBUS_SCAN

#include "masterbus.h"

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome::masterbus {

static const char *const TAG = "masterbus.scan";

/// How long to wait for an answer before treating the question as unanswerable. Measured
/// turnaround on a live bus is under a millisecond, so this is generous by three orders of
/// magnitude - it is the end-of-list signal, not a latency budget.
static constexpr uint32_t ANSWER_TIMEOUT_MS = 200;

/// A device that answers nothing at all should not hold the walk up forever.
static constexpr uint16_t MAX_FIELDS_PER_GROUP = 64;

void MasterbusScanner::start() {
  if (this->is_running())
    return;
  this->device_index_ = 0;
  this->group_ = 0;
  this->field_index_ = 0;
  this->completed_ = 0;
  this->phase_ = Phase::GROUP_FIELD_COUNT;
  this->waiting_ = false;
  this->last_platform_ = nullptr;
  ESP_LOGI(TAG,
           "Walking %u devices for their fields. Paste what follows into your configuration; "
           "where a platform key appears more than once, merge those blocks into one.",
           static_cast<unsigned>(this->hub_->get_discovered_devices().size()));
}

void MasterbusScanner::loop() {
  if (!this->is_running())
    return;
  if (this->device_index_ >= this->hub_->get_discovered_devices().size()) {
    ESP_LOGI(TAG, "Walk finished, %u fields reported", static_cast<unsigned>(this->completed_));
    this->phase_ = Phase::IDLE;
    return;
  }
  const uint32_t now = App.get_loop_component_start_time();
  if (!this->waiting_) {
    this->send_current_();
    this->sent_at_ = now;
    this->waiting_ = true;
  } else if (now - this->sent_at_ >= ANSWER_TIMEOUT_MS) {
    // No answer. For the two list walks that means the end of the list; for a field property it
    // means the field does not carry that one, which is normal - a boolean has no minimum.
    this->advance_(false);
  }
}

void MasterbusScanner::send_current_() {
  const uint32_t address = this->hub_->get_discovered_devices()[this->device_index_].address;
  switch (this->phase_) {
    case Phase::GROUP_FIELD_COUNT:
      this->hub_->request_group(address, MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_COUNT, this->group_);
      break;
    case Phase::GROUP_NAME_ID:
      this->hub_->request_group(address, MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_NAME_STRING, this->group_);
      break;
    case Phase::GROUP_NAME_TEXT:
    case Phase::FIELD_NAME_TEXT:
      this->hub_->request_string(address, this->name_string_, this->chunk_);
      break;
    case Phase::FIELD_NUMBER:
      this->hub_->request_group_index(address, this->group_, this->field_index_);
      break;
    case Phase::FIELD_DISPLAY_TYPE:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_DISPLAY_TYPE, this->field_.param);
      break;
    case Phase::FIELD_NAME_ID:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_NAME_STRING, this->field_.param);
      break;
    case Phase::FIELD_UNIT_ID:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_UNIT_STRING, this->field_.param);
      break;
    case Phase::FIELD_MINIMUM:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_MINIMUM, this->field_.param);
      break;
    case Phase::FIELD_MAXIMUM:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_MAXIMUM, this->field_.param);
      break;
    case Phase::FIELD_STEP:
      this->hub_->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_STEP, this->field_.param);
      break;
    case Phase::FIELD_UNIT_TEXT:
      this->hub_->request_string(address, this->unit_string_, this->chunk_);
      break;
    case Phase::IDLE:
      break;
  }
}

void MasterbusScanner::advance_(bool answered) {
  this->waiting_ = false;
  switch (this->phase_) {
    case Phase::GROUP_FIELD_COUNT:
      if (!answered) {
        // No such group, so this device is done.
        this->next_device_();
        return;
      }
      this->field_index_ = 0;
      this->chunk_ = 0;
      this->name_string_ = 0;
      this->group_name_[0] = '\0';
      this->group_reported_ = false;
      this->phase_ = Phase::GROUP_NAME_ID;
      return;

    case Phase::GROUP_NAME_ID:
      this->chunk_ = 0;
      // A group without a name is normal; its fields are still worth listing.
      this->phase_ = this->name_string_ != 0 ? Phase::GROUP_NAME_TEXT : Phase::FIELD_NUMBER;
      return;

    case Phase::GROUP_NAME_TEXT:
      this->name_string_ = 0;
      this->phase_ = Phase::FIELD_NUMBER;
      return;

    case Phase::FIELD_NUMBER:
      if (!answered || this->field_index_ >= this->fields_in_group_ || this->field_index_ >= MAX_FIELDS_PER_GROUP) {
        // End of this group's field list; try the next group.
        this->group_++;
        this->group_reported_ = false;
        this->phase_ = Phase::GROUP_FIELD_COUNT;
        return;
      }
      this->phase_ = Phase::FIELD_DISPLAY_TYPE;
      return;

    case Phase::FIELD_DISPLAY_TYPE:
      this->phase_ = Phase::FIELD_NAME_ID;
      return;
    case Phase::FIELD_NAME_ID:
      this->phase_ = Phase::FIELD_UNIT_ID;
      return;
    case Phase::FIELD_UNIT_ID:
      this->phase_ = Phase::FIELD_MINIMUM;
      return;
    case Phase::FIELD_MINIMUM:
      this->phase_ = Phase::FIELD_MAXIMUM;
      return;
    case Phase::FIELD_MAXIMUM:
      this->phase_ = Phase::FIELD_STEP;
      return;
    case Phase::FIELD_STEP:
      this->chunk_ = 0;
      this->phase_ = this->name_string_ != 0 ? Phase::FIELD_NAME_TEXT : Phase::FIELD_UNIT_TEXT;
      return;

    case Phase::FIELD_NAME_TEXT:
      if (!answered) {
        this->chunk_ = 0;
        this->phase_ = this->unit_string_ != 0 ? Phase::FIELD_UNIT_TEXT : Phase::IDLE;
        if (this->phase_ == Phase::IDLE)
          this->finish_field_();
        return;
      }
      return;  // more chunks; on_frame decides when the string ended

    case Phase::FIELD_UNIT_TEXT:
      if (!answered) {
        this->finish_field_();
        return;
      }
      return;

    case Phase::IDLE:
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
  this->phase_ = Phase::FIELD_NUMBER;
  this->waiting_ = false;
}

void MasterbusScanner::next_device_() {
  this->device_index_++;
  this->group_ = 0;
  this->group_reported_ = false;
  this->field_index_ = 0;
  this->phase_ = Phase::GROUP_FIELD_COUNT;
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
    case Phase::GROUP_FIELD_COUNT:
      if (type != GROUP_INFORMATION_TYPE || data.size() < 8)
        return false;
      // The count arrives as a float, which is odd for a count but consistent on every group seen.
      this->fields_in_group_ = static_cast<uint16_t>(value32(4));
      break;

    case Phase::GROUP_NAME_ID:
      if (type != GROUP_INFORMATION_TYPE || data.size() < 6)
        return false;
      this->name_string_ = value16(4);
      break;

    case Phase::FIELD_NUMBER:
      if (type != GROUP_INFORMATION_TYPE || data.size() < 6)
        return false;
      this->field_ = {};
      this->field_.group = this->group_;
      this->field_.param = value16(4);
      break;

    case Phase::FIELD_DISPLAY_TYPE:
      if (type != PROPERTY_INFORMATION_TYPE || data.size() < 6)
        return false;
      this->field_.display_type = static_cast<MasterbusDisplayType>(value16(4));
      break;

    case Phase::FIELD_NAME_ID:
      if (type != PROPERTY_INFORMATION_TYPE || data.size() < 6)
        return false;
      this->name_string_ = value16(4);
      break;

    case Phase::FIELD_UNIT_ID:
      if (type != PROPERTY_INFORMATION_TYPE || data.size() < 6)
        return false;
      this->unit_string_ = value16(4);
      break;

    case Phase::FIELD_MINIMUM:
    case Phase::FIELD_MAXIMUM:
    case Phase::FIELD_STEP: {
      if (type != PROPERTY_INFORMATION_TYPE || data.size() < 8)
        return false;
      const float value = value32(4);
      if (this->phase_ == Phase::FIELD_MINIMUM)
        this->field_.minimum = value;
      else if (this->phase_ == Phase::FIELD_MAXIMUM)
        this->field_.maximum = value;
      else
        this->field_.step = value;
      this->field_.has_limits = true;
      break;
    }

    case Phase::GROUP_NAME_TEXT:
    case Phase::FIELD_NAME_TEXT:
    case Phase::FIELD_UNIT_TEXT: {
      if (type == STRING_NOT_AVAILABLE_TYPE) {
        this->advance_(false);
        return true;
      }
      if (type != STRING_INFORMATION_TYPE || data.size() < STRING_CHUNK_LENGTH)
        return false;
      char *out = this->field_.unit;
      uint8_t capacity = SCAN_UNIT_LENGTH;
      if (this->phase_ == Phase::GROUP_NAME_TEXT) {
        out = this->group_name_;
        capacity = SCAN_NAME_LENGTH;
      } else if (this->phase_ == Phase::FIELD_NAME_TEXT) {
        out = this->field_.name;
        capacity = SCAN_NAME_LENGTH;
      }
      const bool ended = this->take_string_chunk_(data, out, capacity);
      this->waiting_ = false;
      if (ended)
        this->advance_(false);  // the string is complete, move on the same way a refusal would
      return true;
    }

    case Phase::IDLE:
      return false;
  }

  this->advance_(true);
  return true;
}

/// Render one field as the configuration a user would write for it. The display type is what
/// picks the platform, which is the whole point of asking for it.
void MasterbusScanner::report_field_() {
  const char *platform = "sensor";
  switch (this->field_.display_type) {
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_CHECKBOX:
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_SWITCH:
      platform = "binary_sensor";
      break;
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_BUTTON:
      platform = "button";
      break;
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_RADIO:
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_DROPDOWN:
      platform = "select";
      break;
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_TEXT:
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_TIME:
    case MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_DATE:
      platform = "text_sensor";
      break;
    default:
      break;
  }
  // The platform key is a YAML mapping key, so printing it for every field would produce a
  // configuration with the same key several times over. Print it only when it changes.
  if (this->last_platform_ == nullptr || strcmp(this->last_platform_, platform) != 0) {
    ESP_LOGI(TAG, "%s:", platform);
    this->last_platform_ = platform;
  }

  const uint32_t address = this->hub_->get_discovered_devices()[this->device_index_].address;
  if (!this->group_reported_) {
    ESP_LOGI(TAG, "  # device 0x%06" PRIX32 ", group %u: %s", address, this->group_,
             this->group_name_[0] != '\0' ? this->group_name_ : "unnamed");
    this->group_reported_ = true;
  }
  ESP_LOGI(TAG, "  - platform: masterbus");
  ESP_LOGI(TAG, "    masterbus_device_id: mb_device_%06" PRIX32, address);
  ESP_LOGI(TAG, "    param: %u", this->field_.param);
  ESP_LOGI(TAG, "    name: \"%s\"", this->field_.name[0] != '\0' ? this->field_.name : "unnamed");
  // A button has no value to poll for, so it takes no cadence. A reading is worth asking for
  // often; a switch or a setting changes rarely.
  if (this->field_.display_type != MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_BUTTON) {
    ESP_LOGI(TAG, "    update_interval: %s",
             this->field_.display_type == MasterbusDisplayType::MASTERBUS_DISPLAY_TYPE_FLOAT ? LOG_STR_LITERAL("10s")
                                                                                             : LOG_STR_LITERAL("5min"));
  }
  if (this->field_.unit[0] != '\0') {
    ESP_LOGI(TAG, "    unit_of_measurement: \"%s\"", this->field_.unit);
  }
  if (this->field_.has_limits && this->field_.maximum > this->field_.minimum) {
    ESP_LOGI(TAG, "    # range %.4g to %.4g step %.4g", this->field_.minimum, this->field_.maximum, this->field_.step);
  }
}

bool MasterbusScanner::take_string_chunk_(const std::vector<uint8_t> &data, char *out, uint8_t capacity) {
  // The header is four bytes; what follows is up to four bytes of ASCII, NUL terminated when the
  // string ends inside this chunk.
  bool ended = data.size() < STRING_CHUNK_LENGTH + 4;
  size_t at = static_cast<size_t>(this->chunk_) * 4;
  for (size_t i = STRING_CHUNK_LENGTH; i < data.size(); i++) {
    if (data[i] == 0) {
      ended = true;
      break;
    }
    if (at < static_cast<size_t>(capacity) - 1)
      out[at++] = static_cast<char>(data[i]);
  }
  out[std::min<size_t>(at, capacity - 1)] = '\0';
  this->chunk_++;
  return ended;
}

}  // namespace esphome::masterbus

#endif
