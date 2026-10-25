#pragma once

#include "esphome/components/canbus/canbus.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "masterbus_info.h"
#include "masterbus_protocol.h"
#include "masterbus_scan.h"
#include "masterbus_text.h"

#include <cinttypes>

namespace esphome::masterbus {

class MasterbusDevice;
class MasterbusHub;

/// The tab's name, for a log line.
const LogString *masterbus_tab_to_string(MasterbusTab tab);

/** One configured field of one MasterBus device, rendered as an ESPHome entity.
 *
 * The address is device, tab and field number. The declared value type is what the entity expects
 * to receive; the hub drops a frame that carries anything else rather than guessing.
 */
class MasterbusEntity : public PollingComponent {
 public:
  MasterbusEntity(MasterbusDevice *device, uint16_t param, MasterbusTab tab, MasterbusValueType value_type)
      : masterbus_device_(device), param_(param), tab_(tab), value_type_(value_type) {}

  /// Ask this entity's device for the field's current value. A MasterBus device answers only when
  /// asked, so this is what makes an entity update on its own rather than on someone else's
  /// polling. Left without an update_interval a PollingComponent never runs, so an entity is
  /// silent until the user asks for a cadence.
  ///
  /// An answer meant for someone else counts: a MasterBus answer is broadcast, so a display panel
  /// or a bridge asking the same device for the same field updates this entity too. When that has
  /// already happened within the cadence there is nothing to gain by asking again, and the request
  /// is skipped - on a busy bus that is most of them.
  ///
  /// Our own answer does not count. It arrives one cadence after the request that caused it, so
  /// letting it stand in for the next request would stretch every interval to twice its length
  /// whenever nobody else is asking - exactly the case the request exists for.
  void update() override;

  /// Note that a value arrived. Whether it answers our own request decides whether it may stand
  /// in for the next one.
  void mark_value_received(uint32_t now) {
    this->last_value_at_ = now;
    this->had_value_ = true;
    this->value_was_ours_ = this->poll_outstanding_;
    this->poll_outstanding_ = false;
    this->stale_ = false;
  }

  /// Report this entity unavailable if its own timeout has run out. Does nothing for an entity
  /// that follows its device, which is what almost every field does.
  void check_stale(uint32_t now);

  /// Publish a value the hub decoded for this entity's field. The hub reads the 32 bits on the wire
  /// as the entity's declared value type and makes the one call that type takes. Code generation
  /// lets a platform declare only the types it can show, so it overrides those calls and no other.
  virtual void publish_masterbus_float(float value) {}
  virtual void publish_masterbus_boolean(bool value) {}
  /// A list option, a device-list entry and an eventable all answer with the index of an entry.
  virtual void publish_masterbus_index(uint32_t index) {}
  /// Text, and a time or a date rendered as text. Rendering is the hub's job, so an entity only
  /// shows what it is handed. The text is only borrowed for the duration of the call.
  virtual void publish_masterbus_text(const char *text) {}
  /// Report that no trustworthy value is available, because the device is unreachable or the
  /// field is unset.
  virtual void publish_masterbus_unavailable() {}

  MasterbusDevice *get_masterbus_device() const { return this->masterbus_device_; }
  uint16_t get_param() const { return this->param_; }
  MasterbusTab get_tab() const { return this->tab_; }
  MasterbusValueType get_value_type() const { return this->value_type_; }
  /// Whether this entity shows that field: a field number means something only on its device's tab.
  bool is_field(const MasterbusDevice *device, MasterbusTab tab, uint16_t param) const {
    return this->masterbus_device_ == device && this->tab_ == tab && this->param_ == param;
  }

  /// Milliseconds without an update before this entity alone goes unavailable. Zero means it
  /// follows its device, which is what almost every field wants. Named apart from
  /// Component::set_timeout, which schedules a callback and is a different thing entirely.
  void set_stale_timeout(uint32_t timeout_ms) { this->stale_timeout_ms_ = timeout_ms; }
  uint32_t get_stale_timeout() const { return this->stale_timeout_ms_; }

  /// Write the field. The first write asks the device whether the field takes one at all (property
  /// 0x0B) and whether it is an event (0x0D), and goes out once it has answered; a field that says
  /// no is not written. With read_back the field is read again shortly afterwards, so the entity
  /// shows what the device kept rather than what was asked for. Returns whether the write, or the
  /// questions before it, reached the bus.
  bool write(float value, bool read_back = true);
  /// The device's word on whether this field takes a write, and on whether it is an event, whoever
  /// asked for it.
  void on_writeable_answer(bool writeable);
  void on_eventable_answer(bool eventable);
  /// Only an event is followed by the frame that names who wrote it: on any other field the next
  /// field number is a field of its own.
  bool is_eventable() const { return this->eventable_ == 1; }

 protected:
  /// The lines every entity adds to its config dump: which field of which device it is.
  void dump_masterbus_config_();
  bool perform_write_(float value, bool read_back);
  /// Send the write that is waiting, once the device has said enough to send it or to refuse it.
  void write_if_answered_();
  /// The device did not answer in time. A field it did not say no to is written anyway, as no
  /// event unless it said so: a write a device declines does nothing, and what went unanswered is
  /// asked again before the next one. On the configuration tab, where the settings are, a field
  /// must have said yes.
  void questions_unanswered_();
  void read_back_();
  /// Say that a write was dropped because the device says the field takes none.
  void warn_takes_no_write_();

  MasterbusDevice *masterbus_device_;
  uint32_t stale_timeout_ms_{0};
  uint32_t last_value_at_{0};
  // A separate flag rather than a zero timestamp: the clock legitimately reads zero.
  bool had_value_{false};
  // We have asked and not yet seen an answer, so the next value to arrive is ours.
  bool poll_outstanding_{false};
  bool value_was_ours_{false};
  // Already reported unavailable for its own timeout, so it is not reported again every second.
  bool stale_{false};
  // Whether the device says the field takes a write, and whether it is an event: -1 not yet known,
  // then 0 or 1. And a write waiting for those answers, with what it is to write.
  int8_t writeable_{-1};
  int8_t eventable_{-1};
  bool write_pending_{false};
  bool pending_read_back_{false};
  float pending_value_{0.0f};
  uint16_t param_;
  MasterbusTab tab_;
  MasterbusValueType value_type_;
};

/** One MasterBus device, addressed by the identifier it announces on the bus.
 *
 * Availability belongs here rather than to the individual entities: field update rates vary
 * enormously within one device, so a per-entity timeout short enough for a current read every
 * second would mark a setting read once an hour permanently dead. When a device goes quiet all of
 * its entities go unavailable together.
 *
 * Online is all a device is: it is online for as long as frames arrive from it. The vendor API
 * defines statuses beyond that, but its library never sets anything but on (see the announcement
 * in masterbus_protocol.h), so there is no status to read.
 */
class MasterbusDevice {
 public:
  /// The timeout is how many milliseconds of silence the device is given before it is treated as
  /// gone, for when its own status reports stop arriving too.
  MasterbusDevice(MasterbusHub *hub, uint32_t address, uint32_t timeout_ms)
      : hub_(hub), address_(address), timeout_ms_(timeout_ms) {}

#ifdef MASTERBUS_ENTITY_COUNT
  void register_entity(MasterbusEntity *entity);
#endif
#ifdef MASTERBUS_INFO_COUNT
  void register_info_sensor(text_sensor::TextSensor *sensor, MasterbusDeviceInfo info);
  /// Note the status bytes of an announcement, at the time it arrived. Returns whether what the
  /// device says about itself should be read again: the device asks for it with bit 5 of byte 6,
  /// and a change of the status version in byte 7 is how the vendor's own tool notices that
  /// something changed. Some equipment flips the version back and forth on nearly every
  /// announcement, so a read asked for within the device's timeout of the last one waits until
  /// an announcement after it.
  bool note_announcement(uint8_t flags, uint8_t status_version, uint32_t now);
  bool has_info() const { return this->has_info_; }
  bool is_info_pending() const { return this->info_pending_; }
  /// Mark the information as wanted, and when it was last asked for.
  void request_info(uint32_t now) {
    this->info_pending_ = true;
    this->info_requested_at_ = now;
    this->info_owed_ = false;
  }
  void set_info_pending(bool pending) { this->info_pending_ = pending; }
#endif

  uint32_t get_address() const { return this->address_; }
  MasterbusHub *get_hub() const { return this->hub_; }

  bool is_online() const { return this->online_; }

  uint32_t get_timeout() const { return this->timeout_ms_; }

  /// Record that the device was heard from, at the time the frame arrived.
  void mark_seen(uint32_t now);
  /// Drop the device to offline. Does nothing if it already is.
  void mark_offline();
  /// Whether the device has been silent for longer than its timeout.
  bool is_timed_out(uint32_t now) const;

  template<typename F> void add_on_online_callback(F &&callback) {
    this->online_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_offline_callback(F &&callback) {
    this->offline_callback_.add(std::forward<F>(callback));
  }

 protected:
  MasterbusHub *hub_;
  LazyCallbackManager<void()> online_callback_;
  LazyCallbackManager<void()> offline_callback_;
  uint32_t address_;
  uint32_t timeout_ms_;
  uint32_t last_seen_{0};
  bool online_{false};
#ifdef MASTERBUS_INFO_COUNT
  bool has_info_{false};
  bool info_pending_{false};
  // The status version last announced, or -1 before the first announcement.
  int8_t status_version_{-1};
  // A read the device asked for too soon after the last one, still to be made.
  bool info_owed_{false};
  uint32_t info_requested_at_{0};
#endif
};

#ifdef USE_MASTERBUS_SCAN
/// How many devices a scan will list. A MasterBus network is a boat's electrical system, not a
/// fieldbus - the largest installation seen carries nine devices.
static constexpr uint8_t SCAN_MAX_DEVICES = 32;

/// One device the scan heard announce itself. Held as data rather than log text so that a test can
/// assert on what was discovered instead of on the wording of a log line.
struct MasterbusDiscoveredDevice {
  uint32_t address;
  uint32_t announcements;
};
#endif

/// The hub's timers. A scan started again before it has reported replaces its own.
static constexpr uint32_t SCAN_REQUEST_TIMEOUT_ID = 0;
static constexpr uint32_t SCAN_REPORT_TIMEOUT_ID = 1;

/** A MasterBus protocol interpreter on top of an existing CAN bus.
 *
 * The hub does not own the CAN hardware. It binds to a `canbus` component, which keeps pin
 * configuration, bit rate and listen-only mode where they already are.
 */
class MasterbusHub : public Component {
 public:
  explicit MasterbusHub(canbus::Canbus *canbus) : canbus_(canbus) {}

  void setup() override;
  void dump_config() override;
#if defined(USE_MASTERBUS_SCAN) || defined(MASTERBUS_INFO_COUNT)
  /// Drives the field walk and the reading of what devices say about themselves. Only those need a
  /// loop, and only while they have something to ask - the hub disables its own once they do not.
  void loop() override;
#endif

  canbus::Canbus *get_canbus() const { return this->canbus_; }

  /// Hand the hub a frame from the bus. Registered as the CAN receive callback in setup(); public
  /// because the frame boundary is the seam this component is exercised through.
  void on_frame(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data);

  /// Ask a device for one field's current value. Returns whether the request reached the bus.
  bool request_field(const MasterbusEntity &entity);

  /// Ask a device for one chunk of one entry in its string table. Both the scan and a text field's
  /// value need it: neither carries text of its own, only the id of an entry in that table.
  bool request_string(uint32_t address, uint16_t string_id, uint8_t chunk);

#ifdef MASTERBUS_UNKNOWN_FRAME_COUNT
  /// Fires for any frame this component reads past without understanding: a message type it cannot
  /// name, or a value message of a length that is neither a read nor an answer, such as another
  /// node's write (see is_undecoded_frame()). The address is the frame's own, which for a write is
  /// the device written to, not the sender. It sits on the hub and not on a device on purpose: the
  /// frames worth catching here are the ones nobody has decoded, and they may well come from
  /// equipment that was never declared - which is exactly the case a device-level trigger cannot see.
  template<typename F> void add_on_unknown_frame_callback(F &&callback) {
    this->unknown_frame_callback_.add(std::forward<F>(callback));
  }
#endif

#ifdef USE_MASTERBUS_SCAN
  /// Every device heard announcing itself since boot, in the order they were first heard.
  const StaticVector<MasterbusDiscoveredDevice, SCAN_MAX_DEVICES> &get_discovered_devices() const {
    return this->discovered_;
  }
#endif
#if defined(USE_MASTERBUS_SCAN) || defined(USE_MASTERBUS_NODE_REQUEST)
  /// Ask every device on the bus to announce itself. A scan does it once; a hub with a node request
  /// interval does it on that timer.
  bool request_nodes();
#endif
#ifdef USE_MASTERBUS_NODE_REQUEST
  void set_node_request_interval(uint32_t interval_ms) { this->node_request_interval_ms_ = interval_ms; }
#endif
#ifdef USE_MASTERBUS_LOG_ALL_FRAMES
  void set_log_all_frames(bool log_all_frames) { this->log_all_frames_ = log_all_frames; }
#endif
#ifdef USE_MASTERBUS_SCAN
  /// Ask every device to announce itself, then walk the ones that did and report them. Started by
  /// the masterbus.scan action, at boot or from a button; ignored while a walk is still running.
  void start_scan();
  /// Write the discovered devices out as configuration the user can paste.
  void report_scan();

  /// Ask a device one of the counts it answers about itself, such as how many groups a tab holds. Equipment that does
  /// not carry a question refuses it outright, which is not an error - see masterbus_protocol.h.
  bool request_general_count(uint32_t address, uint8_t question);

  /// The three questions a scan asks about a device's structure. Each is asked per tab: the four
  /// tabs are the same protocol under different message numbers, so the tab only picks which.
  bool request_group(uint32_t address, MasterbusGroupSelector selector, uint16_t group,
                     MasterbusTab tab = MasterbusTab::MASTERBUS_TAB_MONITORING);
  bool request_group_index(uint32_t address, uint16_t group, uint16_t index,
                           MasterbusTab tab = MasterbusTab::MASTERBUS_TAB_MONITORING);
  /// One entry of a list property: an option's name, a device-list entry, an event's field.
  bool request_property_entry(uint32_t address, MasterbusProperty property, uint16_t param, uint8_t index,
                              MasterbusTab tab = MasterbusTab::MASTERBUS_TAB_MONITORING);

  /// Drive the field walk one step, at the given time. Called from a tick; public so a test can
  /// step it deliberately, and move the clock on to the question a device never answers.
  void scan_step(uint32_t now) { this->scanner_.loop(now); }
  const MasterbusScannedField &get_scanned_field() const { return this->scanner_.get_last_field(); }
  const char *get_scanned_group_name() const { return this->scanner_.get_group_name(); }
#endif
#if defined(USE_MASTERBUS_SCAN) || defined(MASTERBUS_INFO_COUNT)
  /// Ask which string table entry holds one of the device's own strings (GENERAL_STRING_*).
  bool request_general_string(uint32_t address, uint8_t question);
  /// Ask one of a device's processors for its software version.
  bool request_software_version(uint32_t address, uint8_t processor);
#endif

  /// Ask a device for one property of one field.
  bool request_property(uint32_t address, MasterbusProperty property, uint16_t param,
                        MasterbusTab tab = MasterbusTab::MASTERBUS_TAB_MONITORING);

  /// The address this hub speaks as: what it asks for announcements under, and what it writes after
  /// every write as the node that made it. The vendor library's own by default.
  void set_address(uint32_t address) { this->address_ = address; }
  uint32_t get_address() const { return this->address_; }

  /// Set a field to a value, followed by this hub's address on the next field if the entity's field
  /// is an event. A device may still decline to act on it - see the note on the lockout in
  /// masterbus_protocol.h - so a caller that needs the result must read it back.
  bool write_value(const MasterbusEntity &entity, float value);

#ifdef MASTERBUS_DEVICE_COUNT
  void register_device(MasterbusDevice *device) { this->devices_.push_back(device); }
#endif
#ifdef MASTERBUS_INFO_COUNT
  void register_info_sensor(MasterbusDevice *device, MasterbusDeviceInfo info, text_sensor::TextSensor *sensor) {
    this->info_reader_.add_sensor(device, info, sensor);
  }
  /// Read what a device says about itself, as soon as it is online and nothing else is being read.
  void request_device_info(MasterbusDevice *device);
  /// Drive the reading one step, at the given time. Returns whether there is more to do. Public so
  /// a test can step it.
  bool info_step(uint32_t now) { return this->info_reader_.step(now); }
#endif
#ifdef MASTERBUS_ENTITY_COUNT
  void register_entity(MasterbusEntity *entity) { this->entities_.push_back(entity); }
  /// Report every entity of one device as unavailable, in one go.
  void publish_device_unavailable(const MasterbusDevice *device);
#endif
#ifdef MASTERBUS_DEVICE_COUNT
  /// Drop devices that have gone silent past their timeout, and report the fields that have.
  /// Called from a tick; public so a test can drive the clock rather than wait on a scheduler.
  void check_availability(uint32_t now);
#endif

 protected:
#ifdef MASTERBUS_DEVICE_COUNT
  /// The device this identifier belongs to, or nullptr when it is not one we were told about.
  MasterbusDevice *find_device_(uint32_t address);
  /// Take a frame a declared device sent, or that was sent to it.
  void on_device_frame_(MasterbusDevice *device, uint8_t type, const std::vector<uint8_t> &data);
#endif
#ifdef MASTERBUS_ENTITY_COUNT
  /// Take a frame about one of a declared device's fields: a value, or its word on a write.
  void on_entity_frame_(const MasterbusDevice *device, uint8_t type, const std::vector<uint8_t> &data);
  /// Hand a decoded value to every entity configured for that field.
  void publish_value_(const MasterbusDevice *device, MasterbusTab tab, uint16_t param, float value);
#endif
#ifdef USE_MASTERBUS_LOG_ALL_FRAMES
  void log_frame_(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data);
#endif

#ifdef USE_MASTERBUS_SCAN
  /// Note that this address announced itself. Nothing is transmitted here: an announcement answers a
  /// node request, whether this hub's or another node's.
  void record_announcement_(uint32_t address);
#endif

  /// Put one MasterBus message on the bus. Returns whether it was accepted for transmission. The
  /// payload goes straight into the frame, so nothing is allocated per message.
  bool send_(uint8_t type, uint32_t address, std::initializer_list<uint8_t> payload);

  canbus::Canbus *canbus_;
  uint32_t address_{HUB_ADDRESS};
#ifdef USE_MASTERBUS_NODE_REQUEST
  uint32_t node_request_interval_ms_{0};
#endif
#ifdef USE_MASTERBUS_LOG_ALL_FRAMES
  bool log_all_frames_{false};
#endif
#ifdef MASTERBUS_UNKNOWN_FRAME_COUNT
  StaticCallbackManager<MASTERBUS_UNKNOWN_FRAME_COUNT, void(const std::vector<uint8_t> &, uint8_t, uint32_t)>
      unknown_frame_callback_;
#endif
#ifdef USE_MASTERBUS_SCAN
  StaticVector<MasterbusDiscoveredDevice, SCAN_MAX_DEVICES> discovered_;
  MasterbusScanner scanner_{this};
  // Between asking for announcements and reporting them, when each new device is worth a line.
  bool scan_listening_{false};
#endif
#ifdef MASTERBUS_DEVICE_COUNT
  StaticVector<MasterbusDevice *, MASTERBUS_DEVICE_COUNT> devices_;
#endif
#ifdef MASTERBUS_ENTITY_COUNT
  StaticVector<MasterbusEntity *, MASTERBUS_ENTITY_COUNT> entities_;
  /// One bit per tab: whether an answer too short to hold a value has been reported for it.
  uint8_t short_answer_said_{0};
#endif
#ifdef MASTERBUS_INFO_COUNT
  MasterbusInfoReader info_reader_{this};
#endif
#ifdef USE_MASTERBUS_TEXT
  MasterbusTextReader text_reader_{this};
#endif
};

}  // namespace esphome::masterbus
