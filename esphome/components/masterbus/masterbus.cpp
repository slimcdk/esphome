#include "masterbus.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome::masterbus {

static const char *const TAG = "masterbus";

#ifdef USE_MASTERBUS_SCAN
/// How long to listen before reporting. Announcements arrive about three times a second per
/// device, so this is many times longer than it needs to be.
static constexpr uint32_t SCAN_REQUEST_MS = 2000;
static constexpr uint32_t SCAN_SETTLE_MS = 10000;
#endif

#ifdef MASTERBUS_DEVICE_COUNT
/// How often a hub looks for devices that have gone silent. Timeouts are configured in seconds, so
/// checking once a second is as fine grained as it needs to be.
static constexpr uint32_t AVAILABILITY_INTERVAL_MS = 1000;
#endif

#ifdef USE_MASTERBUS_TEXT
/// How long a string read may go unanswered before the field it belongs to is given up on and the
/// slot handed to whoever asks next. Measured turnaround is under a millisecond.
static constexpr uint32_t TEXT_READ_TIMEOUT_MS = 1000;
#endif

const char *masterbus_tab_to_string(MasterbusTab tab) {
  switch (tab) {
    case MasterbusTab::MASTERBUS_TAB_MONITORING:
      return "monitoring";
    case MasterbusTab::MASTERBUS_TAB_ALARM:
      return "alarm";
    case MasterbusTab::MASTERBUS_TAB_HISTORY:
      return "history";
    case MasterbusTab::MASTERBUS_TAB_CONFIGURATION:
      return "configuration";
  }
  return "unknown";
}

#ifdef MASTERBUS_ENTITY_COUNT
void MasterbusDevice::register_entity(MasterbusEntity *entity) { this->hub_->register_entity(entity); }
#endif

void MasterbusDevice::mark_seen(uint32_t now) {
  this->last_seen_ = now;
  if (this->is_online())
    return;
  this->status_ = MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_ON;
  ESP_LOGD(TAG, "Device 0x%06" PRIX32 " is online", this->address_);
  this->online_callback_.call();
}

void MasterbusDevice::mark_offline() {
  if (!this->is_online())
    return;
  ESP_LOGW(TAG, "Device 0x%06" PRIX32 " stopped responding", this->address_);
  this->status_ = MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_OFFLINE;
#ifdef MASTERBUS_ENTITY_COUNT
  this->hub_->publish_device_unavailable(this);
#endif
  this->offline_callback_.call();
}

bool MasterbusDevice::is_timed_out(uint32_t now) const { return now - this->last_seen_ >= this->timeout_ms_; }

bool MasterbusHub::send_(uint8_t type, uint32_t address, std::initializer_list<uint8_t> payload) {
  this->tx_.assign(payload);
  const uint32_t can_id = (static_cast<uint32_t>(type) << MESSAGE_TYPE_SHIFT) | address;
  return this->canbus_->send_data(can_id, true, false, this->tx_) == canbus::ERROR_OK;
}

bool MasterbusHub::request_string(uint32_t address, uint16_t string_id, uint8_t chunk) {
  return this->send_(
      STRING_REQUEST_TYPE, address,
      {STRING_REQUEST_MARKER, static_cast<uint8_t>(string_id & 0xFF), static_cast<uint8_t>(string_id >> 8), chunk});
}

#ifdef USE_MASTERBUS_SCAN
void MasterbusHub::record_announcement_(uint32_t address) {
  for (auto &found : this->discovered_) {
    if (found.address == address) {
      found.announcements++;
      return;
    }
  }
  if (this->discovered_.size() == MASTERBUS_SCAN_MAX_DEVICES) {
    ESP_LOGW(TAG, "Scan is full at %d devices; 0x%06" PRIX32 " and any further ones are not listed",
             MASTERBUS_SCAN_MAX_DEVICES, address);
    return;
  }
  ESP_LOGI(TAG, "Scan found device 0x%06" PRIX32, address);
  this->discovered_.push_back({address, 1});
}

bool MasterbusHub::request_nodes() {
  bool sent = false;
  // Repeated the way the vendor library repeats it, so a device that missed one still answers.
  for (uint8_t i = 0; i < NODE_REQUEST_REPEATS; i++)
    sent |= this->send_(NODE_REQUEST_TYPE, NODE_REQUEST_ADDRESS, {});
  if (!sent) {
    ESP_LOGW(TAG, "Could not put the node request on the bus. Scanning falls back to listening for "
                  "devices that announce themselves unprompted.");
  }
  return sent;
}

bool MasterbusHub::request_group(uint32_t address, MasterbusGroupSelector selector, uint16_t group, MasterbusTab tab) {
  return this->send_(
      masterbus_request_type(group_message(tab)), address,
      {static_cast<uint8_t>(selector), static_cast<uint8_t>(group & 0xFF), static_cast<uint8_t>(group >> 8)});
}

bool MasterbusHub::request_group_index(uint32_t address, uint16_t group, uint16_t index, MasterbusTab tab) {
  return this->send_(
      masterbus_request_type(group_message(tab)), address,
      {static_cast<uint8_t>(MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_AT_INDEX),
       static_cast<uint8_t>(group & 0xFF), static_cast<uint8_t>(group >> 8), static_cast<uint8_t>(index)});
}

bool MasterbusHub::request_property(uint32_t address, MasterbusProperty property, uint16_t param, MasterbusTab tab) {
  return this->send_(
      masterbus_request_type(property_message(tab)), address,
      {static_cast<uint8_t>(property), static_cast<uint8_t>(param & 0xFF), static_cast<uint8_t>(param >> 8)});
}

void MasterbusHub::report_scan() {
  if (this->discovered_.empty()) {
    ESP_LOGW(TAG, "Scan heard nothing. Check wiring, bit rate and termination - 60 ohm across "
                  "CAN_H and CAN_L with everything powered down.");
    return;
  }
  ESP_LOGI(TAG, "Scan found %u devices. Paste the block below under your masterbus hub:",
           static_cast<unsigned>(this->discovered_.size()));
  ESP_LOGI(TAG, "  devices:");
  for (const auto &found : this->discovered_) {
    ESP_LOGI(TAG, "    - id: mb_device_%06" PRIX32, found.address);
    ESP_LOGI(TAG, "      device: 0x%06" PRIX32, found.address);
  }
  // Walking each device for its fields is the second half of the scan, and it transmits.
  this->scanner_.start();
  this->enable_loop();
}

void MasterbusHub::loop() {
  this->scanner_.loop(App.get_loop_component_start_time());
  // The walk sends one question and waits for its answer, so it wants the loop for as long as it
  // runs and not a moment longer. Before it starts and after it finishes there is nothing to do.
  if (!this->scanner_.is_running())
    this->disable_loop();
}
#endif

void MasterbusHub::setup() {
  // The transmit buffer never grows after this: a MasterBus payload is at most a full CAN frame.
  this->tx_.reserve(canbus::CAN_MAX_DATA_LENGTH);
  this->canbus_->add_callback([this](uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
    this->on_frame(can_id, extended_id, rtr, data);
  });
#ifdef MASTERBUS_DEVICE_COUNT
  this->set_interval(AVAILABILITY_INTERVAL_MS,
                     [this]() { this->check_availability(App.get_loop_component_start_time()); });
#endif
#ifdef USE_MASTERBUS_SCAN
  // Ask once the bus has settled after boot, then report what answered. A device that announces
  // itself unprompted is picked up either way, but asking is what makes the list complete.
  this->set_timeout(SCAN_REQUEST_MS, [this]() { this->request_nodes(); });
  this->set_timeout(SCAN_SETTLE_MS, [this]() { this->report_scan(); });
#endif
}

void MasterbusHub::on_frame(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
#ifdef USE_MASTERBUS_LOG_ALL_FRAMES
  this->log_frame_(can_id, extended_id, rtr, data);
#endif
#if defined(USE_MASTERBUS_SCAN) || defined(MASTERBUS_DEVICE_COUNT) || defined(MASTERBUS_UNKNOWN_FRAME_COUNT)
  // Every MasterBus message rides an extended identifier. A remote transmission request carries
  // no payload of its own, so there is nothing in it to decode either way.
  if (!extended_id || rtr)
    return;
  const uint32_t address = can_id & DEVICE_ADDRESS_MASK;
  const uint8_t type = can_id >> MESSAGE_TYPE_SHIFT;
#ifdef MASTERBUS_UNKNOWN_FRAME_COUNT
  // Before anything is decoded, because the frames this is for are the ones nothing below will
  // look at. It runs ahead of the device list for the same reason the scan does.
  if (!is_known_message_type(type))
    this->unknown_frame_callback_.call(data, type, address);
#endif
#ifdef USE_MASTERBUS_SCAN
  // A scan listens on its own account. It must not sit behind the device list: the whole point of
  // scanning is to find devices nobody has declared yet, so a configuration with `scan: true` and
  // no `devices:` is exactly the case that has to work.
  if (type == DEVICE_ANNOUNCEMENT_TYPE && data.size() >= DEVICE_ANNOUNCEMENT_LENGTH)
    this->record_announcement_(decode_announced_address(data.data()));
  this->scanner_.on_frame(type, address, data);
#endif
#ifdef MASTERBUS_DEVICE_COUNT
  MasterbusDevice *device = this->find_device_(address);
  if (device == nullptr)
    return;
  // Only a frame the device sent proves it is there. A request carries the address it is aimed
  // at, so another node asking a dead device for something would otherwise keep it alive.
  if (device_sent_message(type))
    device->mark_seen(App.get_loop_component_start_time());

#ifdef MASTERBUS_ENTITY_COUNT
#ifdef USE_MASTERBUS_TEXT
  if (this->take_text_frame_(type, address, data))
    return;
#endif
  // Which tab the value belongs to is carried by the message number and nothing else, so it is
  // read back out here and handed on: an entity only takes a value from its own tab.
  MasterbusTab tab;
  if (!data_information_tab(type, tab))
    return;
  if (data.size() < MONITORING_INFORMATION_LENGTH) {
    // Every tab is assumed to carry a value the way monitoring does, which is the one thing about
    // the other tabs that has not been watched. A tab where it does not hold would drop every
    // answer in silence, so it is said - once, because it is the assumption that is wrong and not
    // the frame, and a poll repeats.
    const uint8_t already_said = 1 << static_cast<uint8_t>(tab);
    if ((this->short_answer_said_ & already_said) == 0) {
      this->short_answer_said_ |= already_said;
      ESP_LOGW(TAG, "Device 0x%06" PRIX32 " answered with %u bytes on the %s tab, fewer than a value needs", address,
               static_cast<unsigned>(data.size()), masterbus_tab_to_string(tab));
    }
    return;
  }
  const uint16_t param = encode_uint16(data[1], data[0]);
  const uint32_t bits = encode_uint32(data[5], data[4], data[3], data[2]);
  float value;
  memcpy(&value, &bits, sizeof(value));
  this->publish_value_(device, tab, param, value);
#endif
#endif
#endif
}

void MasterbusHub::dump_config() {
  ESP_LOGCONFIG(TAG, "MasterBus:");
#ifdef USE_MASTERBUS_SCAN
  ESP_LOGCONFIG(TAG, "  Scan: enabled");
#endif
#ifdef USE_MASTERBUS_LOG_ALL_FRAMES
  ESP_LOGCONFIG(TAG, "  Raw frame logging: enabled");
#endif
#ifdef MASTERBUS_DEVICE_COUNT
  for (auto *device : this->devices_) {
    ESP_LOGCONFIG(TAG, "  Device 0x%06" PRIX32 ", timeout %" PRIu32 " ms", device->get_address(),
                  device->get_timeout());
  }
#endif
}

void MasterbusEntity::update() {
  const uint32_t now = App.get_loop_component_start_time();
  // Somebody else already asked recently and we heard the answer, so asking again would add
  // traffic without adding information. An answer to our own request is a cadence old by now and
  // says nothing about whether anyone else is covering this field.
  if (this->had_value_ && !this->value_was_ours_ && now - this->last_value_at_ < this->get_update_interval())
    return;
  // A request that never reached the bus leaves nothing to wait for. Latching the flag anyway
  // would make the next answer from another node look like ours and suppress the poll after it.
  this->poll_outstanding_ = this->device_->get_hub()->request_field(*this);
}

void MasterbusEntity::check_stale(uint32_t now) {
  // Nothing to report for a field that follows its device, has never had a value, or has already
  // been reported. A device that has gone quiet altogether reports through the device instead.
  if (this->stale_timeout_ms_ == 0 || !this->had_value_ || this->stale_ || !this->device_->is_online())
    return;
  if (now - this->last_value_at_ < this->stale_timeout_ms_)
    return;
  ESP_LOGD(TAG, "Field %u of device 0x%06" PRIX32 " has not been answered for %" PRIu32 " ms", this->param_,
           this->device_->get_address(), this->stale_timeout_ms_);
  this->stale_ = true;
  this->publish_masterbus_unavailable();
}

bool MasterbusHub::request_field(const MasterbusEntity &entity) {
  MasterbusMessage message;
  if (!tab_data_message(entity.get_tab(), message)) {
    // The alarm tab. Its structure can be walked, but the vendor carries its values in a message
    // filed with the broadcast ones rather than with the tabs, and that message is not decoded.
    ESP_LOGW(TAG, "Cannot poll field %u on the %s tab: the message that carries its values is not decoded",
             entity.get_param(), masterbus_tab_to_string(entity.get_tab()));
    return false;
  }
  const uint16_t param = entity.get_param();
  ESP_LOGV(TAG, "Asking device 0x%06" PRIX32 " for field %u on the %s tab",
           entity.get_masterbus_device()->get_address(), param, masterbus_tab_to_string(entity.get_tab()));
  return this->send_(masterbus_request_type(message), entity.get_masterbus_device()->get_address(),
                     {static_cast<uint8_t>(param & 0xFF), static_cast<uint8_t>(param >> 8)});
}

bool MasterbusHub::write_boolean(const MasterbusEntity &entity, bool state) {
  return this->write_value(entity, state ? 1.0f : 0.0f);
}

bool MasterbusHub::write_value(const MasterbusEntity &entity, float value) {
  // Reading a tab and writing it are separate questions. The write frame and the commit that
  // follows it were captured on monitoring event fields and nowhere else, and the configuration
  // tab is where a wrong frame changes a charger's voltage. Writing stays refused off monitoring
  // until a write has been watched on that tab.
  if (entity.get_tab() != MasterbusTab::MASTERBUS_TAB_MONITORING) {
    ESP_LOGW(TAG, "Cannot write field %u on the %s tab: only monitoring writes have been verified", entity.get_param(),
             masterbus_tab_to_string(entity.get_tab()));
    return false;
  }
  const uint16_t param = entity.get_param();
  const uint32_t address = entity.get_masterbus_device()->get_address();
  ESP_LOGD(TAG, "Writing %.4g to field %u of device 0x%06" PRIX32, value, param, address);

  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  if (!this->send_(
          MONITORING_REQUEST_TYPE, address,
          {static_cast<uint8_t>(param & 0xFF), static_cast<uint8_t>(param >> 8), static_cast<uint8_t>(bits & 0xFF),
           static_cast<uint8_t>(bits >> 8), static_cast<uint8_t>(bits >> 16), static_cast<uint8_t>(bits >> 24)})) {
    ESP_LOGW(TAG, "Could not put the write for field %u on the bus", param);
    return false;
  }

  // The vendor library always follows a write with this second frame, addressed to the next field
  // number. See masterbus_protocol.h: it is sent because the library sends it.
  const uint16_t commit_param = static_cast<uint16_t>(param + 1);
  const uint8_t *commit = monitoring_write_commit();
  if (!this->send_(MONITORING_REQUEST_TYPE, address,
                   {static_cast<uint8_t>(commit_param & 0xFF), static_cast<uint8_t>(commit_param >> 8), commit[0],
                    commit[1], commit[2], commit[3]})) {
    ESP_LOGW(TAG, "Wrote field %u but could not send the frame that follows it", param);
    return false;
  }
  return true;
}

#ifdef MASTERBUS_DEVICE_COUNT
MasterbusDevice *MasterbusHub::find_device_(uint32_t address) {
  for (auto *device : this->devices_) {
    if (device->get_address() == address)
      return device;
  }
  return nullptr;
}

void MasterbusHub::check_availability(uint32_t now) {
  for (auto *device : this->devices_) {
    if (device->is_timed_out(now))
      device->mark_offline();
  }
#ifdef MASTERBUS_ENTITY_COUNT
  // A device that is still answering can hold a field that is not. Only the entities given a
  // timeout of their own are asked; the rest follow their device.
  for (auto *entity : this->entities_)
    entity->check_stale(now);
#endif
}
#endif

#ifdef MASTERBUS_ENTITY_COUNT
void MasterbusHub::publish_value_(const MasterbusDevice *device, MasterbusTab tab, uint16_t param, float value) {
  for (auto *entity : this->entities_) {
    if (entity->get_masterbus_device() != device || entity->get_tab() != tab || entity->get_param() != param)
      continue;
    // The answer arrived, whatever we end up able to make of it. Recording that before decoding
    // is what clears the outstanding poll, so a field whose value needs a second read is not left
    // waiting on an answer that already came.
    entity->mark_value_received(App.get_loop_component_start_time());
    if (std::isnan(value)) {
      entity->publish_masterbus_unavailable();
      continue;
    }

    // Monitoring always arrives as a float on the wire. The entity's declared type says how to
    // read it, so a checkbox field becomes a boolean rather than a 1.0.
    MasterbusValue decoded{};
    decoded.type = entity->get_value_type();
    // Where a number that publishes as text is rendered. Not value_text_: that holds a string
    // read that may be in flight, and a chunk landing on a rendered number ruins both.
    char text[16];
    switch (decoded.type) {
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT:
        decoded.as_float = value;
        break;
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN:
        decoded.as_boolean = value != 0.0f;
        break;
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_LIST_OPTION:
        decoded.as_raw = static_cast<uint32_t>(value);
        break;
#ifdef USE_MASTERBUS_TEXT
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT:
        // A text field answers with the number of an entry in the device's string table, not with
        // the text. Reading that entry is a second exchange, so the entity is published from
        // finish_text_read_() rather than from here.
        this->begin_text_read_(entity, static_cast<uint16_t>(value));
        continue;
#endif
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME: {
        MasterbusTime time{};
        if (!decode_time(value, time)) {
          ESP_LOGW(TAG, "Field %u of device 0x%06" PRIX32 " answered %.6g, which is not a time", param,
                   device->get_address(), value);
          entity->publish_masterbus_unavailable();
          continue;
        }
        snprintf(text, sizeof(text), "%02u:%02u:%02u", time.hour, time.minute, time.second);
        decoded.as_text = text;
        break;
      }
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE: {
        MasterbusDate date{};
        if (!decode_date(value, date)) {
          ESP_LOGW(TAG, "Field %u of device 0x%06" PRIX32 " answered %.6g, which is not a date", param,
                   device->get_address(), value);
          entity->publish_masterbus_unavailable();
          continue;
        }
        snprintf(text, sizeof(text), "%04u-%02u-%02u", date.year, date.month, date.day);
        decoded.as_text = text;
        break;
      }
      default:
        // Device identifier and eventable have no entity that declares them.
        entity->publish_masterbus_unavailable();
        continue;
    }
    entity->publish_masterbus_value(decoded);
  }
}

void MasterbusHub::publish_device_unavailable(const MasterbusDevice *device) {
#ifdef USE_MASTERBUS_TEXT
  // The device that owes us the rest of a string has stopped answering, so nothing is coming.
  if (this->text_entity_ != nullptr && this->text_entity_->get_masterbus_device() == device)
    this->text_entity_ = nullptr;
#endif
  for (auto *entity : this->entities_) {
    if (entity->get_masterbus_device() == device)
      entity->publish_masterbus_unavailable();
  }
}
#endif

#ifdef USE_MASTERBUS_TEXT
void MasterbusHub::begin_text_read_(MasterbusEntity *entity, uint16_t string_id) {
  if (string_id == 0) {
    // A device answers zero for a field whose text it does not hold.
    entity->publish_masterbus_unavailable();
    return;
  }
  const uint32_t now = App.get_loop_component_start_time();
  if (this->text_entity_ != nullptr) {
    if (now - this->text_sent_at_ < TEXT_READ_TIMEOUT_MS) {
      // A device answers one question at a time. Whichever field lost the race keeps the text it
      // already has and asks again on its own cadence.
      ESP_LOGV(TAG, "Field %u waits for the string read already in flight", entity->get_param());
      return;
    }
    this->finish_text_read_(false);
  }

  this->text_entity_ = entity;
  this->text_string_ = string_id;
  this->text_sent_at_ = now;
  this->value_text_[0] = '\0';
  if (!this->request_string(entity->get_masterbus_device()->get_address(), string_id, 0)) {
    ESP_LOGW(TAG, "Could not ask for the text of field %u", entity->get_param());
    this->finish_text_read_(false);
  }
}

bool MasterbusHub::take_text_frame_(uint8_t type, uint32_t address, const std::vector<uint8_t> &data) {
  if (this->text_entity_ == nullptr || address != this->text_entity_->get_masterbus_device()->get_address())
    return false;
  if (type != STRING_INFORMATION_TYPE && type != STRING_NOT_AVAILABLE_TYPE)
    return false;
  // The answer echoes the request header, so it says which string and which chunk it carries.
  // Without that check a chunk of somebody else's string lands in the middle of ours.
  if (data.size() < STRING_CHUNK_LENGTH || data[0] != STRING_REQUEST_MARKER ||
      encode_uint16(data[2], data[1]) != this->text_string_)
    return false;

  if (type == STRING_NOT_AVAILABLE_TYPE) {
    this->finish_text_read_(false);
    return true;
  }
  if (take_string_chunk(data.data(), data.size(), this->value_text_, MASTERBUS_TEXT_LENGTH)) {
    this->finish_text_read_(true);
    return true;
  }
  // More to come. The chunk number is the answer's, not a counter of our own.
  this->text_sent_at_ = App.get_loop_component_start_time();
  if (!this->request_string(address, this->text_string_, data[3] + 1))
    this->finish_text_read_(false);
  return true;
}

void MasterbusHub::finish_text_read_(bool found) {
  MasterbusEntity *entity = this->text_entity_;
  this->text_entity_ = nullptr;
  if (entity == nullptr)
    return;
  if (!found || this->value_text_[0] == '\0') {
    ESP_LOGD(TAG, "Field %u has no text under string %u", entity->get_param(), this->text_string_);
    entity->publish_masterbus_unavailable();
    return;
  }
  MasterbusValue decoded{};
  decoded.type = MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT;
  decoded.as_text = this->value_text_;
  entity->publish_masterbus_value(decoded);
}
#endif

#ifdef USE_MASTERBUS_LOG_ALL_FRAMES
void MasterbusHub::log_frame_(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
  char payload[format_hex_pretty_size(canbus::CAN_MAX_DATA_LENGTH)];
  format_hex_pretty_to(payload, data.data(), std::min<size_t>(data.size(), canbus::CAN_MAX_DATA_LENGTH));
  ESP_LOGD(TAG, "%s 0x%08" PRIX32 "%s [%u] %s", extended_id ? LOG_STR_LITERAL("ext") : LOG_STR_LITERAL("std"), can_id,
           rtr ? LOG_STR_LITERAL(" rtr") : LOG_STR_LITERAL(""), static_cast<unsigned>(data.size()), payload);
}
#endif

}  // namespace esphome::masterbus
