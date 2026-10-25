#include "masterbus_text.h"

#ifdef USE_MASTERBUS_TEXT

#include "masterbus.h"

#include "esphome/core/log.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus");

/// How long a string read may go unanswered before the field it belongs to is given up on and the
/// slot handed to whoever asks next. Measured turnaround is under a millisecond.
static constexpr uint32_t TEXT_READ_TIMEOUT_MS = 1000;

void MasterbusTextReader::begin(MasterbusEntity *entity, uint16_t string_id, uint32_t now) {
  if (string_id == 0) {
    // A device answers zero for a field whose text it does not hold.
    entity->publish_masterbus_unavailable();
    return;
  }
  if (this->entity_ != nullptr) {
    if (now - this->sent_at_ < TEXT_READ_TIMEOUT_MS) {
      // Whichever field lost the race keeps the text it already has and asks again on its own cadence.
      ESP_LOGV(TAG, "Field %u waits for the string read already in flight", entity->get_param());
      return;
    }
    this->finish_(false);
  }

  this->entity_ = entity;
  this->sent_at_ = now;
  this->string_.begin(string_id, this->text_, STRING_BUFFER_LENGTH);
  if (!this->hub_->request_string(entity->get_masterbus_device()->get_address(), string_id, 0)) {
    ESP_LOGW(TAG, "Could not ask for the text of field %u", entity->get_param());
    this->finish_(false);
  }
}

bool MasterbusTextReader::on_frame(uint8_t type, uint32_t address, const std::vector<uint8_t> &data, uint32_t now) {
  if (this->entity_ == nullptr || address != this->entity_->get_masterbus_device()->get_address())
    return false;
  switch (this->string_.take(type, data.data(), data.size())) {
    case MasterbusStringRead::Result::RESULT_NOT_OURS:
      return false;
    case MasterbusStringRead::Result::RESULT_REFUSED:
      this->finish_(false);
      return true;
    case MasterbusStringRead::Result::RESULT_DONE:
      this->finish_(true);
      return true;
    case MasterbusStringRead::Result::RESULT_MORE:
      break;
  }
  this->sent_at_ = now;
  if (!this->hub_->request_string(address, this->string_.get_id(), this->string_.get_chunk()))
    this->finish_(false);
  return true;
}

void MasterbusTextReader::forget(const MasterbusDevice *device) {
  if (this->entity_ != nullptr && this->entity_->get_masterbus_device() == device)
    this->entity_ = nullptr;
}

void MasterbusTextReader::finish_(bool found) {
  MasterbusEntity *entity = this->entity_;
  this->entity_ = nullptr;
  if (entity == nullptr)
    return;
  if (!found || this->text_[0] == '\0') {
    ESP_LOGD(TAG, "Field %u has no text under string %u", entity->get_param(), this->string_.get_id());
    entity->publish_masterbus_unavailable();
    return;
  }
  if (this->string_.is_truncated()) {
    ESP_LOGW(TAG, "Text of field %u is longer than %u characters and is published cut short", entity->get_param(),
             static_cast<unsigned>(STRING_BUFFER_LENGTH - 1));
  }
  entity->publish_masterbus_text(this->text_);
}

}  // namespace esphome::masterbus

#endif
