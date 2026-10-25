#include "masterbus.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus");

#ifdef USE_MASTERBUS_SCAN
/// How long to listen before reporting. Every device answers a node request within a few
/// milliseconds, so this is many times longer than it needs to be.
static constexpr uint32_t SCAN_REQUEST_MS = 2000;
static constexpr uint32_t SCAN_SETTLE_MS = 10000;
#endif

#ifdef MASTERBUS_DEVICE_COUNT
/// How often a hub looks for devices that have gone silent. Timeouts are configured in seconds, so
/// checking once a second is as fine grained as it needs to be.
static constexpr uint32_t AVAILABILITY_INTERVAL_MS = 1000;
#endif

/// How long a write waits for the device to say whether the field takes one and whether it is an
/// event. A property answer comes back within milliseconds.
static constexpr uint32_t WRITEABLE_TIMEOUT_MS = 500;
/// How long after a write the field is read again. The device answers at once, but may take a
/// moment to act on a command - a contactor has to move.
static constexpr uint32_t READ_BACK_DELAY_MS = 200;
static constexpr uint32_t WRITEABLE_TIMEOUT_ID = 0;
static constexpr uint32_t READ_BACK_TIMEOUT_ID = 1;

const LogString *masterbus_tab_to_string(MasterbusTab tab) {
  switch (tab) {
    case MasterbusTab::MASTERBUS_TAB_MONITORING:
      return LOG_STR("monitoring");
    case MasterbusTab::MASTERBUS_TAB_ALARM:
      return LOG_STR("alarm");
    case MasterbusTab::MASTERBUS_TAB_HISTORY:
      return LOG_STR("history");
    case MasterbusTab::MASTERBUS_TAB_CONFIGURATION:
      return LOG_STR("configuration");
  }
  return LOG_STR("unknown");
}

#ifdef MASTERBUS_ENTITY_COUNT
void MasterbusDevice::register_entity(MasterbusEntity *entity) { this->hub_->register_entity(entity); }
#endif

#ifdef MASTERBUS_INFO_COUNT
void MasterbusDevice::register_info_sensor(text_sensor::TextSensor *sensor, MasterbusDeviceInfo info) {
  this->has_info_ = true;
  this->hub_->register_info_sensor(this, info, sensor);
}

bool MasterbusDevice::note_announcement(uint8_t flags, uint8_t status_version, uint32_t now) {
  const int8_t version = static_cast<int8_t>(status_version & ANNOUNCEMENT_STATUS_VERSION_MASK);
  const bool changed = this->status_version_ >= 0 && version != this->status_version_;
  this->status_version_ = version;
  if (!this->has_info_)
    return false;
  if ((flags & ANNOUNCEMENT_READ_AGAIN_BIT) != 0 || changed)
    this->info_owed_ = true;
  return this->info_owed_ && !this->info_pending_ && now - this->info_requested_at_ >= this->timeout_ms_;
}
#endif

void MasterbusDevice::mark_seen(uint32_t now) {
  this->last_seen_ = now;
  if (this->is_online())
    return;
  this->online_ = true;
  ESP_LOGD(TAG, "Device 0x%06" PRIX32 " is online", this->address_);
#ifdef MASTERBUS_INFO_COUNT
  // A device that comes back may have been changed or replaced while it was away.
  if (this->has_info_)
    this->hub_->request_device_info(this);
#endif
  this->online_callback_.call();
}

void MasterbusDevice::mark_offline() {
  if (!this->is_online())
    return;
  ESP_LOGW(TAG, "Device 0x%06" PRIX32 " stopped responding", this->address_);
  this->online_ = false;
#ifdef MASTERBUS_ENTITY_COUNT
  this->hub_->publish_device_unavailable(this);
#endif
  this->offline_callback_.call();
}

bool MasterbusDevice::is_timed_out(uint32_t now) const { return now - this->last_seen_ >= this->timeout_ms_; }

bool MasterbusHub::send_(uint8_t type, uint32_t address, std::initializer_list<uint8_t> payload) {
  const uint32_t can_id = (static_cast<uint32_t>(type) << MESSAGE_TYPE_SHIFT) | address;
  // A bus that is off, or whose queue is full, refuses everything until it recovers. The warning
  // stays up for as long as that lasts and is logged once, when it is raised.
  if (this->canbus_->send_data(can_id, true, false, payload.begin(), payload.size()) != canbus::ERROR_OK) {
    this->status_set_warning(LOG_STR("The CAN bus refused a frame"));
    return false;
  }
  this->status_clear_warning();
  return true;
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
  if (this->discovered_.size() == SCAN_MAX_DEVICES) {
    ESP_LOGW(TAG, "Scan is full at %u devices; 0x%06" PRIX32 " and any further ones are not listed",
             static_cast<unsigned>(SCAN_MAX_DEVICES), address);
    return;
  }
  if (this->scan_listening_) {
    ESP_LOGI(TAG, "Scan found device 0x%06" PRIX32, address);
  }
  this->discovered_.push_back({address, 1});
}
#endif

#if defined(USE_MASTERBUS_SCAN) || defined(USE_MASTERBUS_NODE_REQUEST)
bool MasterbusHub::request_nodes() {
  // Said once for each time the bus stops taking frames, not at every interval while it does not.
  const bool already_refused = this->status_has_warning();
  bool sent = false;
  // Repeated the way the vendor library repeats it, so a device that missed one still answers.
  for (uint8_t i = 0; i < NODE_REQUEST_REPEATS; i++)
    sent |= this->send_(NODE_REQUEST_TYPE, this->address_, {});
  if (!sent && !already_refused) {
    ESP_LOGW(TAG, "Could not put the node request on the bus. Devices then announce themselves only when "
                  "another node asks, and where none does, nothing is heard from them until they are polled.");
  }
  return sent;
}
#endif

#if defined(USE_MASTERBUS_SCAN) || defined(MASTERBUS_INFO_COUNT)
bool MasterbusHub::request_general_string(uint32_t address, uint8_t question) {
  return this->send_(STRING_REQUEST_TYPE, address, {GENERAL_STRING_SELECTOR, question});
}

bool MasterbusHub::request_software_version(uint32_t address, uint8_t processor) {
  return this->send_(STRING_REQUEST_TYPE, address, {SOFTWARE_VERSION_SELECTOR, processor});
}
#endif

#ifdef USE_MASTERBUS_SCAN
bool MasterbusHub::request_general_count(uint32_t address, uint8_t question) {
  return this->send_(STRING_REQUEST_TYPE, address, {GENERAL_COUNT_SELECTOR, question});
}

bool MasterbusHub::request_group(uint32_t address, MasterbusGroupSelector selector, uint16_t group, MasterbusTab tab) {
  return this->send_(
      tab_message(tab, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_GROUP).request, address,
      {static_cast<uint8_t>(selector), static_cast<uint8_t>(group & 0xFF), static_cast<uint8_t>(group >> 8)});
}

bool MasterbusHub::request_group_index(uint32_t address, uint16_t group, uint16_t index, MasterbusTab tab) {
  return this->send_(
      tab_message(tab, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_GROUP).request, address,
      {static_cast<uint8_t>(MasterbusGroupSelector::MASTERBUS_GROUP_SELECTOR_FIELD_AT_INDEX),
       static_cast<uint8_t>(group & 0xFF), static_cast<uint8_t>(group >> 8), static_cast<uint8_t>(index)});
}

bool MasterbusHub::request_property_entry(uint32_t address, MasterbusProperty property, uint16_t param, uint8_t index,
                                          MasterbusTab tab) {
  return this->send_(
      tab_message(tab, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_PROPERTY).request, address,
      {static_cast<uint8_t>(property), static_cast<uint8_t>(param & 0xFF), static_cast<uint8_t>(param >> 8), index});
}

void MasterbusHub::start_scan() {
  // The walk reads the list as it goes, so it is not emptied under it.
  if (this->scanner_.is_running()) {
    ESP_LOGW(TAG, "A scan is still walking the devices; not started again");
    return;
  }
  // Ask once the bus has settled, then report what answered. A device another node asks about is
  // picked up either way, but asking ourselves is what makes the list complete.
  this->discovered_.clear();
  this->scan_listening_ = true;
  this->set_timeout(SCAN_REQUEST_TIMEOUT_ID, SCAN_REQUEST_MS, [this]() { this->request_nodes(); });
  this->set_timeout(SCAN_REPORT_TIMEOUT_ID, SCAN_SETTLE_MS, [this]() { this->report_scan(); });
}

void MasterbusHub::report_scan() {
  this->scan_listening_ = false;
  if (this->discovered_.empty()) {
    ESP_LOGW(TAG, "Scan heard nothing. Check wiring, bit rate and termination - 60 ohm across "
                  "CAN_H and CAN_L with everything powered down.");
    return;
  }
  // Each device was named as it announced itself, and the walk carries the address on every line
  // it writes, so there is nothing to repeat here. Only the walk's lines are configuration.
  ESP_LOGI(TAG,
           "Scan found %u devices. The lines that follow go into the converter on the "
           "documentation page, which turns them into configuration.",
           static_cast<unsigned>(this->discovered_.size()));
  // Walking each device for its fields is the second half of the scan, and it transmits.
  this->scanner_.start();
  this->enable_loop();
}

#endif

#if defined(USE_MASTERBUS_SCAN) || defined(MASTERBUS_INFO_COUNT)
void MasterbusHub::loop() {
  const uint32_t now = App.get_loop_component_start_time();
  bool busy = false;
#ifdef USE_MASTERBUS_SCAN
  this->scanner_.loop(now);
  busy = this->scanner_.is_running();
#endif
#ifdef MASTERBUS_INFO_COUNT
  busy |= this->info_step(now);
#endif
  // Each sends one question and waits for its answer, so they want the loop for as long as they
  // have something to ask and not a moment longer.
  if (!busy)
    this->disable_loop();
}
#endif

void MasterbusHub::setup() {
  this->canbus_->add_callback([this](uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
    this->on_frame(can_id, extended_id, rtr, data);
  });
#ifdef MASTERBUS_DEVICE_COUNT
  this->set_interval(AVAILABILITY_INTERVAL_MS,
                     [this]() { this->check_availability(App.get_loop_component_start_time()); });
#endif
#ifdef USE_MASTERBUS_NODE_REQUEST
  // An announcement is what keeps a device nobody polls online. Where no other node asks for them,
  // this hub does. The define is global, so a second hub given no interval still gets here.
  if (this->node_request_interval_ms_ != 0)
    this->set_interval(this->node_request_interval_ms_, [this]() { this->request_nodes(); });
#endif
}

void MasterbusHub::on_frame(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
#ifdef USE_MASTERBUS_LOG_ALL_FRAMES
  if (this->log_all_frames_)
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
  if (is_undecoded_frame(type, data.size()))
    this->unknown_frame_callback_.call(data, type, address);
#endif
#ifdef USE_MASTERBUS_SCAN
  // A scan listens on its own account. It must not sit behind the device list: the whole point of
  // scanning is to find devices nobody has declared yet, so a configuration that scans and declares
  // no `devices:` is exactly the case that has to work.
  if (type == DEVICE_ANNOUNCEMENT_TYPE && data.size() >= DEVICE_ANNOUNCEMENT_LENGTH)
    this->record_announcement_(decode_announced_address(data.data()));
  this->scanner_.on_frame(type, address, data);
#endif
#ifdef MASTERBUS_DEVICE_COUNT
  MasterbusDevice *device = this->find_device_(address);
  if (device != nullptr)
    this->on_device_frame_(device, type, data);
#endif
#endif
}

#ifdef MASTERBUS_DEVICE_COUNT
void MasterbusHub::on_device_frame_(MasterbusDevice *device, uint8_t type, const std::vector<uint8_t> &data) {
  const uint32_t now = App.get_loop_component_start_time();
  // Only a frame the device sent proves it is there. A request carries the address it is aimed
  // at, so another node asking a dead device for something would otherwise keep it alive.
  if (device_sent_message(type))
    device->mark_seen(now);
#ifdef MASTERBUS_INFO_COUNT
  if (type == DEVICE_ANNOUNCEMENT_TYPE && data.size() >= DEVICE_ANNOUNCEMENT_LENGTH &&
      device->note_announcement(data[6], data[7], now))
    this->request_device_info(device);
  if (this->info_reader_.on_frame(type, device->get_address(), data))
    return;
#endif
#ifdef MASTERBUS_ENTITY_COUNT
  this->on_entity_frame_(device, type, data);
#endif
}
#endif

#ifdef MASTERBUS_ENTITY_COUNT
void MasterbusHub::on_entity_frame_(const MasterbusDevice *device, uint8_t type, const std::vector<uint8_t> &data) {
  const uint32_t address = device->get_address();
#ifdef USE_MASTERBUS_TEXT
  if (this->text_reader_.on_frame(type, address, data, App.get_loop_component_start_time()))
    return;
#endif
  // A device's word on whether a field takes a write, or is an event, is broadcast like any answer,
  // so it counts whoever asked for it.
  MasterbusTab tab;
  if (property_information_tab(type, tab) && data.size() >= PROPERTY_NUMBER_ANSWER_LENGTH &&
      (data[0] == static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_WRITEABLE) ||
       data[0] == static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_EVENTABLE))) {
    const bool writeable_answer = data[0] == static_cast<uint8_t>(MasterbusProperty::MASTERBUS_PROPERTY_WRITEABLE);
    const uint16_t field = encode_uint16(data[2], data[1]);
    const bool yes = encode_uint16(data[5], data[4]) != 0;
    for (auto *entity : this->entities_) {
      if (!entity->is_field(device, tab, field))
        continue;
      if (writeable_answer) {
        entity->on_writeable_answer(yes);
      } else {
        entity->on_eventable_answer(yes);
      }
    }
    return;
  }
  // Which tab the value belongs to is carried by the message number and nothing else, so it is
  // read back out here and handed on: an entity only takes a value from its own tab.
  if (!data_information_tab(type, tab))
    return;
  if (data.size() < MONITORING_INFORMATION_LENGTH) {
    // Every tab has been watched answering with a field number and a value. Equipment that answers
    // shorter on some tab would have every answer dropped in silence, so it is said - once, because
    // it is the equipment and not the frame that differs, and a poll repeats.
    const uint8_t already_said = 1 << static_cast<uint8_t>(tab);
    if ((this->short_answer_said_ & already_said) == 0) {
      this->short_answer_said_ |= already_said;
      ESP_LOGW(TAG, "Device 0x%06" PRIX32 " answered with %u bytes on the %s tab, fewer than a value needs", address,
               static_cast<unsigned>(data.size()), LOG_STR_ARG(masterbus_tab_to_string(tab)));
    }
    return;
  }
  const uint16_t param = encode_uint16(data[1], data[0]);
  const uint32_t bits = encode_uint32(data[5], data[4], data[3], data[2]);
  float value;
  memcpy(&value, &bits, sizeof(value));
  this->publish_value_(device, tab, param, value);
}
#endif

void MasterbusHub::dump_config() {
  ESP_LOGCONFIG(TAG,
                "MasterBus:\n"
                "  Address: 0x%06" PRIX32,
                this->address_);
#ifdef USE_MASTERBUS_LOG_ALL_FRAMES
  if (this->log_all_frames_) {
    ESP_LOGCONFIG(TAG, "  Raw frame logging: enabled");
  }
#endif
#ifdef USE_MASTERBUS_NODE_REQUEST
  if (this->node_request_interval_ms_ != 0) {
    ESP_LOGCONFIG(TAG, "  Node requests: every %" PRIu32 " ms", this->node_request_interval_ms_);
  }
#endif
#ifdef MASTERBUS_DEVICE_COUNT
  for (auto *device : this->devices_) {
    ESP_LOGCONFIG(TAG, "  Device 0x%06" PRIX32 ", timeout %" PRIu32 " ms", device->get_address(),
                  device->get_timeout());
  }
#endif
#ifdef MASTERBUS_INFO_COUNT
  this->info_reader_.dump_config();
#endif
}

void MasterbusEntity::dump_masterbus_config_() {
  ESP_LOGCONFIG(TAG,
                "  Device: 0x%06" PRIX32 "\n"
                "  Tab: %s\n"
                "  Field: %u",
                this->masterbus_device_->get_address(), LOG_STR_ARG(masterbus_tab_to_string(this->tab_)), this->param_);
  if (this->stale_timeout_ms_ != 0) {
    ESP_LOGCONFIG(TAG, "  Timeout: %" PRIu32 " ms", this->stale_timeout_ms_);
  }
  LOG_UPDATE_INTERVAL(this);
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
  this->poll_outstanding_ = this->masterbus_device_->get_hub()->request_field(*this);
}

void MasterbusEntity::check_stale(uint32_t now) {
  // Nothing to report for a field that follows its device, has never had a value, or has already
  // been reported. A device that has gone quiet altogether reports through the device instead.
  if (this->stale_timeout_ms_ == 0 || !this->had_value_ || this->stale_ || !this->masterbus_device_->is_online())
    return;
  if (now - this->last_value_at_ < this->stale_timeout_ms_)
    return;
  ESP_LOGD(TAG, "Field %u of device 0x%06" PRIX32 " has not been answered for %" PRIu32 " ms", this->param_,
           this->masterbus_device_->get_address(), this->stale_timeout_ms_);
  this->stale_ = true;
  this->publish_masterbus_unavailable();
}

bool MasterbusHub::request_field(const MasterbusEntity &entity) {
  const uint16_t param = entity.get_param();
  ESP_LOGV(TAG, "Asking device 0x%06" PRIX32 " for field %u on the %s tab",
           entity.get_masterbus_device()->get_address(), param, LOG_STR_ARG(masterbus_tab_to_string(entity.get_tab())));
  return this->send_(tab_message(entity.get_tab(), MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA).request,
                     entity.get_masterbus_device()->get_address(),
                     {static_cast<uint8_t>(param & 0xFF), static_cast<uint8_t>(param >> 8)});
}

bool MasterbusHub::request_property(uint32_t address, MasterbusProperty property, uint16_t param, MasterbusTab tab) {
  return this->send_(
      tab_message(tab, MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_PROPERTY).request, address,
      {static_cast<uint8_t>(property), static_cast<uint8_t>(param & 0xFF), static_cast<uint8_t>(param >> 8)});
}

bool MasterbusEntity::write(float value, bool read_back) {
  if (this->writeable_ == MasterbusAnswer::MASTERBUS_ANSWER_NO) {
    this->warn_takes_no_write_();
    return false;
  }
  if (this->writeable_ == MasterbusAnswer::MASTERBUS_ANSWER_YES &&
      this->eventable_ != MasterbusAnswer::MASTERBUS_ANSWER_UNKNOWN)
    return this->perform_write_(value, read_back);
  // A write already waiting for the answers just takes the newer value.
  const bool asked = this->write_pending_;
  this->write_pending_ = true;
  this->pending_value_ = value;
  this->pending_read_back_ = read_back;
  if (asked)
    return true;
  // Only what is not yet known is asked.
  MasterbusHub *hub = this->masterbus_device_->get_hub();
  const uint32_t address = this->masterbus_device_->get_address();
  bool sent = true;
  if (this->writeable_ == MasterbusAnswer::MASTERBUS_ANSWER_UNKNOWN)
    sent = hub->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_WRITEABLE, this->param_, this->tab_);
  if (sent)
    sent = hub->request_property(address, MasterbusProperty::MASTERBUS_PROPERTY_EVENTABLE, this->param_, this->tab_);
  if (!sent) {
    this->write_pending_ = false;
    ESP_LOGW(TAG, "Could not ask about field %u before writing it", this->param_);
    return false;
  }
  this->set_timeout(WRITEABLE_TIMEOUT_ID, WRITEABLE_TIMEOUT_MS, [this]() { this->questions_unanswered_(); });
  return true;
}

void MasterbusEntity::on_writeable_answer(bool writeable) {
  this->writeable_ = masterbus_answer(writeable);
  this->write_if_answered_();
}

void MasterbusEntity::on_eventable_answer(bool eventable) {
  this->eventable_ = masterbus_answer(eventable);
  this->write_if_answered_();
}

void MasterbusEntity::write_if_answered_() {
  // A no is enough to refuse; a yes waits to hear whether the field is an event.
  if (!this->write_pending_ || this->writeable_ == MasterbusAnswer::MASTERBUS_ANSWER_UNKNOWN ||
      (this->writeable_ == MasterbusAnswer::MASTERBUS_ANSWER_YES &&
       this->eventable_ == MasterbusAnswer::MASTERBUS_ANSWER_UNKNOWN))
    return;
  this->write_pending_ = false;
  this->cancel_timeout(WRITEABLE_TIMEOUT_ID);
  if (this->writeable_ == MasterbusAnswer::MASTERBUS_ANSWER_YES) {
    this->perform_write_(this->pending_value_, this->pending_read_back_);
    return;
  }
  this->warn_takes_no_write_();
}

void MasterbusEntity::warn_takes_no_write_() {
  ESP_LOGW(TAG, "Field %u of device 0x%06" PRIX32 " takes no write, by the device's own word; not written",
           this->param_, this->masterbus_device_->get_address());
}

void MasterbusEntity::questions_unanswered_() {
  if (!this->write_pending_)
    return;
  this->write_pending_ = false;
  if (this->writeable_ == MasterbusAnswer::MASTERBUS_ANSWER_UNKNOWN) {
    // A configuration field is a setting, and the vendor's own tool never writes one the device has
    // not said it takes. It is asked again before the next write.
    if (this->tab_ == MasterbusTab::MASTERBUS_TAB_CONFIGURATION) {
      ESP_LOGW(TAG,
               "Field %u on the configuration tab of device 0x%06" PRIX32
               " did not say whether it takes a write; not written",
               this->param_, this->masterbus_device_->get_address());
      return;
    }
    ESP_LOGD(TAG, "Field %u did not say whether it takes a write; writing it anyway", this->param_);
  }
  this->perform_write_(this->pending_value_, this->pending_read_back_);
}

bool MasterbusEntity::perform_write_(float value, bool read_back) {
  if (!this->masterbus_device_->get_hub()->write_value(*this, value))
    return false;
  if (read_back)
    this->set_timeout(READ_BACK_TIMEOUT_ID, READ_BACK_DELAY_MS, [this]() { this->read_back_(); });
  return true;
}

void MasterbusEntity::read_back_() {
  // Our own request, so the answer to it does not stand in for the next poll.
  this->poll_outstanding_ = this->masterbus_device_->get_hub()->request_field(*this);
}

bool MasterbusHub::write_value(const MasterbusEntity &entity, float value) {
  // A write rides the tab's own request number, on every tab: that is how the vendor library
  // writes. Only monitoring writes have been watched on a wire.
  const uint8_t type = tab_message(entity.get_tab(), MasterbusTabMessage::MASTERBUS_TAB_MESSAGE_DATA).request;
  const uint16_t param = entity.get_param();
  const uint32_t address = entity.get_masterbus_device()->get_address();
  ESP_LOGD(TAG, "Writing %.4g to field %u on the %s tab of device 0x%06" PRIX32, value, param,
           LOG_STR_ARG(masterbus_tab_to_string(entity.get_tab())), address);

  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  if (!this->send_(
          type, address,
          {static_cast<uint8_t>(param & 0xFF), static_cast<uint8_t>(param >> 8), static_cast<uint8_t>(bits & 0xFF),
           static_cast<uint8_t>(bits >> 8), static_cast<uint8_t>(bits >> 16), static_cast<uint8_t>(bits >> 24)})) {
    ESP_LOGW(TAG, "Could not put the write for field %u on the bus", param);
    return false;
  }

  // An event is followed by who set it off, on the next field number, the way the vendor library
  // writes one. On any other field that number is a field of its own and is left alone.
  if (!entity.is_eventable())
    return true;
  const uint16_t source_param = static_cast<uint16_t>(param + 1);
  if (!this->send_(type, address,
                   {static_cast<uint8_t>(source_param & 0xFF), static_cast<uint8_t>(source_param >> 8),
                    static_cast<uint8_t>(this->address_ & 0xFF), static_cast<uint8_t>(this->address_ >> 8),
                    static_cast<uint8_t>(this->address_ >> 16), static_cast<uint8_t>(this->address_ >> 24)})) {
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
    if (!entity->is_field(device, tab, param))
      continue;
    // The answer arrived, whatever we end up able to make of it. Recording that before decoding
    // is what clears the outstanding poll, so a field whose value needs a second read is not left
    // waiting on an answer that already came.
    entity->mark_value_received(App.get_loop_component_start_time());
    if (std::isnan(value)) {
      entity->publish_masterbus_unavailable();
      continue;
    }

    // Where a number that publishes as text is rendered. Not value_text_: that holds a string
    // read that may be in flight, and a chunk landing on a rendered number ruins both.
    char text[16];
    // Monitoring always arrives as a float on the wire. The entity's declared type says how to
    // read it, so a checkbox field becomes a boolean rather than a 1.0.
    switch (entity->get_value_type()) {
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_FLOAT:
        entity->publish_masterbus_float(value);
        continue;
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_BOOLEAN:
        entity->publish_masterbus_boolean(value != 0.0f);
        continue;
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_LIST_OPTION:
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_DEVICE_ID:
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_EVENTABLE:
        // Each answers with the index of an entry in a list the device holds. One that is negative,
        // infinite or too large to be an index is no entry, rather than cast round to some other.
        entity->publish_masterbus_index(value >= 0.0f && value < 4294967296.0f ? static_cast<uint32_t>(value)
                                                                               : UINT32_MAX);
        continue;
#ifdef USE_MASTERBUS_TEXT
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT:
        // A text field answers with the number of an entry in the device's string table, not with
        // the text. Reading that entry is a second exchange, so the entity is published by the text
        // reader rather than from here. A string id is two bytes, so anything else names no entry
        // at all. Not a number was dealt with above.
        if (value < 0.0f || value > 65535.0f) {
          entity->publish_masterbus_unavailable();
          continue;
        }
        // Reading the string is a request like any other, and an entity given no update_interval
        // never transmits. Another node's answer for it is left at that.
        if (value != 0.0f && entity->get_update_interval() == SCHEDULER_DONT_RUN) {
          ESP_LOGV(TAG, "Field %u never polls, so its string %u is not read", param, static_cast<unsigned>(value));
          continue;
        }
        this->text_reader_.begin(entity, static_cast<uint16_t>(value), App.get_loop_component_start_time());
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
        entity->publish_masterbus_text(text);
        continue;
      }
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE: {
        // Zero is a date the device leaves unset, which is ordinary and not worth a warning.
        if (value == 0.0f) {
          entity->publish_masterbus_unavailable();
          continue;
        }
        MasterbusDate date{};
        if (!decode_date(value, date)) {
          ESP_LOGW(TAG, "Field %u of device 0x%06" PRIX32 " answered %.6g, which is not a date", param,
                   device->get_address(), value);
          entity->publish_masterbus_unavailable();
          continue;
        }
        snprintf(text, sizeof(text), "%04u-%02u-%02u", date.year, date.month, date.day);
        entity->publish_masterbus_text(text);
        continue;
      }
      default:
        entity->publish_masterbus_unavailable();
        continue;
    }
  }
}

void MasterbusHub::publish_device_unavailable(const MasterbusDevice *device) {
#ifdef USE_MASTERBUS_TEXT
  this->text_reader_.forget(device);
#endif
  for (auto *entity : this->entities_) {
    if (entity->get_masterbus_device() == device)
      entity->publish_masterbus_unavailable();
  }
}
#endif

#ifdef MASTERBUS_INFO_COUNT
void MasterbusHub::request_device_info(MasterbusDevice *device) {
  device->request_info(App.get_loop_component_start_time());
  this->enable_loop();
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
