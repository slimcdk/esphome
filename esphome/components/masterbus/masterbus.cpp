#include "masterbus.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
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

const char *masterbus_device_status_to_string(MasterbusDeviceStatus status) {
  switch (status) {
    case MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_OFFLINE:
      return "offline";
    case MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_SLEEPING:
      return "sleeping";
    case MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_ON:
      return "on";
    case MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_ON_WARNING:
      return "warning";
    case MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_OFF_FAULT:
      return "fault";
    case MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_OFF_ERROR:
      return "error";
    case MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_UPDATING:
      return "updating";
  }
  return "unknown";
}

#ifdef MASTERBUS_ENTITY_COUNT
void MasterbusDevice::register_entity(MasterbusEntity *entity) { this->hub_->register_entity(entity); }
#endif

void MasterbusDevice::mark_seen(MasterbusDeviceStatus status) {
  this->last_seen_ = App.get_loop_component_start_time();
  if (status == MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_OFFLINE) {
    // A device that reports itself offline is gone as surely as one that stopped answering.
    this->mark_offline();
    return;
  }
  const bool was_offline = !this->is_online();
  this->status_ = status;
  if (!was_offline)
    return;
  ESP_LOGD(TAG, "Device 0x%06" PRIX32 " is online (%s)", this->address_, masterbus_device_status_to_string(status));
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

bool MasterbusDevice::is_timed_out(uint32_t now) const {
  // A device that has never been heard from is already offline, so it cannot time out.
  return this->last_seen_ != 0 && now - this->last_seen_ >= this->timeout_ms_;
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
  const uint32_t can_id = (static_cast<uint32_t>(NODE_REQUEST_TYPE) << MESSAGE_TYPE_SHIFT) | NODE_REQUEST_ADDRESS;
  bool sent = false;
  // Repeated the way the vendor library repeats it, so a device that missed one still answers.
  for (uint8_t i = 0; i < NODE_REQUEST_REPEATS; i++)
    sent |= this->canbus_->send_data(can_id, true, false, {}) == canbus::ERROR_OK;
  if (!sent) {
    ESP_LOGW(TAG, "Could not put the node request on the bus. Scanning falls back to listening for "
                  "devices that announce themselves unprompted.");
  }
  return sent;
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
  ESP_LOGI(TAG, "Field numbers are not listed: reading a device's field list is not implemented yet.");
}
#endif

void MasterbusHub::setup() {
  this->canbus_->add_callback([this](uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
    this->on_frame(can_id, extended_id, rtr, data);
  });
#ifdef MASTERBUS_DEVICE_COUNT
  this->set_interval(AVAILABILITY_INTERVAL_MS, [this]() { this->check_availability_(); });
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
#ifdef MASTERBUS_DEVICE_COUNT
  // Every MasterBus message rides an extended identifier. A remote transmission request carries
  // no payload of its own, so there is nothing in it to decode either way.
  if (!extended_id || rtr)
    return;
  const uint32_t address = can_id & DEVICE_ADDRESS_MASK;
#ifdef USE_MASTERBUS_SCAN
  if ((can_id >> MESSAGE_TYPE_SHIFT) == DEVICE_ANNOUNCEMENT_TYPE && data.size() >= DEVICE_ANNOUNCEMENT_LENGTH)
    this->record_announcement_(decode_announced_address(data.data()));
#endif
  MasterbusDevice *device = this->find_device_(address);
  if (device == nullptr)
    return;
  // Any frame at all proves the device is answering, whatever it turns out to say.
  device->mark_seen(MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_ON);

#ifdef MASTERBUS_ENTITY_COUNT
  if ((can_id >> MESSAGE_TYPE_SHIFT) != MONITORING_INFORMATION_TYPE || data.size() < MONITORING_INFORMATION_LENGTH)
    return;
  const uint16_t param = encode_uint16(data[1], data[0]);
  const uint32_t bits = encode_uint32(data[5], data[4], data[3], data[2]);
  float value;
  memcpy(&value, &bits, sizeof(value));
  this->publish_value_(device, MasterbusTab::MASTERBUS_TAB_MONITORING, param, value);
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

void MasterbusEntity::update() { this->device_->get_hub()->request_field(*this); }

bool MasterbusHub::request_field(const MasterbusEntity &entity) {
  // Only monitoring requests have a known layout.
  if (entity.get_tab() != MasterbusTab::MASTERBUS_TAB_MONITORING) {
    ESP_LOGW(TAG, "Cannot poll field %u on the %s tab: no request format is known for it", entity.get_param(),
             masterbus_tab_to_string(entity.get_tab()));
    return false;
  }
  const uint16_t param = entity.get_param();
  const std::vector<uint8_t> payload{static_cast<uint8_t>(param & 0xFF), static_cast<uint8_t>(param >> 8)};
  const uint32_t can_id = (static_cast<uint32_t>(MONITORING_REQUEST_TYPE) << MESSAGE_TYPE_SHIFT) |
                          entity.get_masterbus_device()->get_address();
  return this->canbus_->send_data(can_id, true, false, payload) == canbus::ERROR_OK;
}

bool MasterbusHub::write_boolean(const MasterbusEntity &entity, bool state) {
  // The vendor API exposes exactly one write call, for boolean fields, but no frame it produces
  // has been recorded yet. Until one is, refuse loudly rather than put a guess on a bus that
  // controls battery relays.
  ESP_LOGW(TAG, "Cannot set field %u of device 0x%06" PRIX32 " to %s: the MasterBus write frame is not known yet",
           entity.get_param(), entity.get_masterbus_device()->get_address(),
           state ? LOG_STR_LITERAL("on") : LOG_STR_LITERAL("off"));
  return false;
}

#ifdef MASTERBUS_DEVICE_COUNT
MasterbusDevice *MasterbusHub::find_device_(uint32_t address) {
  for (auto *device : this->devices_) {
    if (device->get_address() == address)
      return device;
  }
  return nullptr;
}

void MasterbusHub::check_availability_() {
  const uint32_t now = App.get_loop_component_start_time();
  for (auto *device : this->devices_) {
    if (device->is_timed_out(now))
      device->mark_offline();
  }
}
#endif

#ifdef MASTERBUS_ENTITY_COUNT
void MasterbusHub::publish_value_(const MasterbusDevice *device, MasterbusTab tab, uint16_t param, float value) {
  for (auto *entity : this->entities_) {
    if (entity->get_masterbus_device() != device || entity->get_tab() != tab || entity->get_param() != param)
      continue;
    // Monitoring always arrives as a float on the wire. The entity's declared type says how to
    // read it, so a checkbox field becomes a boolean rather than a 1.0.
    MasterbusValue decoded{};
    decoded.type = entity->get_value_type();
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
      default:
        // Text, time and date do not fit a six byte monitoring frame; nothing here can carry them.
        entity->publish_masterbus_unavailable();
        continue;
    }
    if (std::isnan(value)) {
      entity->publish_masterbus_unavailable();
      continue;
    }
    entity->publish_masterbus_value(decoded);
  }
}

void MasterbusHub::publish_device_unavailable(const MasterbusDevice *device) {
  for (auto *entity : this->entities_) {
    if (entity->get_masterbus_device() == device)
      entity->publish_masterbus_unavailable();
  }
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
