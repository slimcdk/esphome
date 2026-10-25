#include "masterbus_info.h"

#ifdef MASTERBUS_INFO_COUNT

#include "masterbus.h"

#include "esphome/core/log.h"

#include <cstdio>
#include <cstring>

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.info");

/// How long one question about a device may go unanswered before it is taken as not carried.
static constexpr uint32_t INFO_TIMEOUT_MS = 1000;

static constexpr uint8_t FIRMWARE = static_cast<uint8_t>(MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_FIRMWARE);

bool MasterbusInfoReader::step(uint32_t now) {
  if (this->device_ == nullptr) {
    for (const auto &entry : this->sensors_) {
      if (entry.device->is_info_pending() && entry.device->is_online()) {
        this->device_ = entry.device;
        this->item_ = 0;
        this->next_();
        break;
      }
    }
    if (this->device_ == nullptr)
      return false;
  }
  if (this->waiting_) {
    if (now - this->sent_at_ < INFO_TIMEOUT_MS)
      return true;
    // Silence: the device does not carry this one. A firmware read keeps the processors it heard.
    this->publish_(this->item_ == FIRMWARE && this->processor_ > 0 ? this->text_ : nullptr);
    this->next_();
    if (this->device_ == nullptr)
      return true;
  }

  const uint32_t address = this->device_->get_address();
  bool sent;
  if (this->item_ == FIRMWARE) {
    sent = this->hub_->request_software_version(address, this->processor_);
  } else if (this->have_string_) {
    sent = this->hub_->request_string(address, this->string_.get_id(), this->string_.get_chunk());
  } else {
    sent = this->hub_->request_general_string(address, this->item_);
  }
  if (!sent) {
    // The bus would not take it. Leave the device pending and try again on a later pass.
    this->device_ = nullptr;
    return true;
  }
  this->waiting_ = true;
  this->sent_at_ = now;
  return true;
}

void MasterbusInfoReader::next_() {
  this->waiting_ = false;
  this->have_string_ = false;
  this->processor_ = 0;
  this->text_[0] = '\0';
  // The next fact some sensor shows for this device.
  for (this->item_++; this->item_ <= FIRMWARE; this->item_++) {
    for (const auto &entry : this->sensors_) {
      if (entry.device == this->device_ && static_cast<uint8_t>(entry.info) == this->item_)
        return;
    }
  }
  this->device_->set_info_pending(false);
  this->device_ = nullptr;
}

bool MasterbusInfoReader::on_frame(uint8_t type, uint32_t address, const std::vector<uint8_t> &data) {
  if (this->device_ == nullptr || !this->waiting_ || address != this->device_->get_address())
    return false;
  if (type != STRING_INFORMATION_TYPE && type != STRING_NOT_AVAILABLE_TYPE)
    return false;
  const bool refused = type == STRING_NOT_AVAILABLE_TYPE;

  if (this->item_ == FIRMWARE) {
    if (!is_software_version_header(data.data(), data.size(), this->processor_))
      return false;
    if (!refused && data.size() >= SOFTWARE_VERSION_INFORMATION_LENGTH && this->processor_ < MAX_PROCESSORS) {
      // One version per processor, as the device numbers them: high byte, a dot, low byte.
      const size_t at = strlen(this->text_);
      snprintf(this->text_ + at, sizeof(this->text_) - at, "%s%u.%u", at == 0 ? "" : " ",
               static_cast<unsigned>(data[3]), static_cast<unsigned>(data[2]));
      this->processor_++;
      this->waiting_ = false;
      if (this->processor_ < MAX_PROCESSORS)
        return true;
    }
    // Refused, or as many as are kept: the list of processors is done.
    this->publish_(this->processor_ > 0 ? this->text_ : nullptr);
    this->next_();
    return true;
  }

  if (!this->have_string_) {
    if (!is_general_string_header(data.data(), data.size(), this->item_))
      return false;
    if (refused || data.size() < GENERAL_STRING_INFORMATION_LENGTH) {
      this->publish_(nullptr);
      this->next_();
      return true;
    }
    this->string_.begin(encode_uint16(data[3], data[2]), this->text_, STRING_BUFFER_LENGTH);
    this->have_string_ = true;
    this->waiting_ = false;
    return true;
  }

  switch (this->string_.take(type, data.data(), data.size())) {
    case MasterbusStringRead::Result::RESULT_NOT_OURS:
      return false;
    case MasterbusStringRead::Result::RESULT_REFUSED:
      this->publish_(nullptr);
      this->next_();
      return true;
    case MasterbusStringRead::Result::RESULT_DONE:
      this->publish_(this->text_[0] != '\0' ? this->text_ : nullptr);
      this->next_();
      return true;
    case MasterbusStringRead::Result::RESULT_MORE:
      break;
  }
  this->waiting_ = false;
  return true;
}

void MasterbusInfoReader::publish_(const char *text) {
  for (const auto &entry : this->sensors_) {
    if (entry.device != this->device_ || static_cast<uint8_t>(entry.info) != this->item_)
      continue;
    // A text sensor has no way to clear a published string, so the state flag is all that can be
    // dropped.
    if (text == nullptr) {
      entry.sensor->set_has_state(false);
    } else {
      entry.sensor->publish_state(text);
    }
  }
}

void MasterbusInfoReader::dump_config() {
  for (const auto &entry : this->sensors_) {
    LOG_TEXT_SENSOR("  ", "Device information", entry.sensor);
    ESP_LOGCONFIG(TAG, "    Device: 0x%06" PRIX32, entry.device->get_address());
  }
}

}  // namespace esphome::masterbus

#endif
