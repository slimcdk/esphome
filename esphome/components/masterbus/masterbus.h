#pragma once

#include "esphome/components/canbus/canbus.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

#include "masterbus_protocol.h"

#include <cinttypes>

namespace esphome::masterbus {

class MasterbusDevice;
class MasterbusHub;

/** A decoded field value.
 *
 * The text, time and date types all arrive as `as_text`: rendering them is the decoder's job,
 * so an entity only ever has to display what it is handed. That text points into the decoder's
 * own buffer and is only valid for the duration of the publish call; an entity that needs to
 * keep it must copy it.
 */
struct MasterbusValue {
  MasterbusValueType type;
  union {
    float as_float;
    bool as_boolean;
    uint32_t as_raw;      // time, date, list index and device identifier all arrive as 32 bits
    const char *as_text;  // NUL terminated, used for the text, time and date types
  };
};

/** One configured field of one MasterBus device, rendered as an ESPHome entity.
 *
 * The address is device, tab and field number. The declared value type is what the entity expects
 * to receive; the hub drops a frame that carries anything else rather than guessing.
 */
class MasterbusEntity : public PollingComponent {
 public:
  MasterbusEntity(MasterbusDevice *device, uint16_t param, MasterbusTab tab, MasterbusValueType value_type)
      : device_(device), param_(param), tab_(tab), value_type_(value_type) {}

  /// Ask this entity's device for the field's current value. A MasterBus device answers only when
  /// asked, so this is what makes an entity update on its own rather than on someone else's
  /// polling. Left without an update_interval a PollingComponent never runs, so an entity is
  /// silent until the user asks for a cadence.
  void update() override;

  /// Publish a value the hub decoded for this entity's field.
  virtual void publish_masterbus_value(const MasterbusValue &value) = 0;
  /// Report that no trustworthy value is available, because the device is unreachable or the
  /// field is unset.
  virtual void publish_masterbus_unavailable() = 0;

  MasterbusDevice *get_masterbus_device() const { return this->device_; }
  uint16_t get_param() const { return this->param_; }
  MasterbusTab get_tab() const { return this->tab_; }
  MasterbusValueType get_value_type() const { return this->value_type_; }

  /// Milliseconds without an update before this entity alone goes unavailable. Zero means it
  /// follows its device, which is what almost every field wants. Named apart from
  /// Component::set_timeout, which schedules a callback and is a different thing entirely.
  void set_stale_timeout(uint32_t timeout_ms) { this->stale_timeout_ms_ = timeout_ms; }
  uint32_t get_stale_timeout() const { return this->stale_timeout_ms_; }

 protected:
  MasterbusDevice *device_;
  uint32_t stale_timeout_ms_{0};
  uint16_t param_;
  MasterbusTab tab_;
  MasterbusValueType value_type_;
};

/** One MasterBus device, addressed by the identifier it announces on the bus.
 *
 * Availability belongs here rather than to the individual entities: field update rates vary
 * enormously within one device, so a per-entity timeout would mark a relay that changes twice a
 * day permanently dead. When a device goes quiet all of its entities go unavailable together.
 */
class MasterbusDevice {
 public:
  MasterbusDevice(MasterbusHub *hub, uint32_t address) : hub_(hub), address_(address) {}

#ifdef MASTERBUS_ENTITY_COUNT
  void register_entity(MasterbusEntity *entity);
#endif

  uint32_t get_address() const { return this->address_; }
  MasterbusHub *get_hub() const { return this->hub_; }

  MasterbusDeviceStatus get_status() const { return this->status_; }
  bool is_online() const { return this->status_ != MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_OFFLINE; }

  /// Milliseconds of silence after which the device is treated as gone, for when its own status
  /// reports stop arriving too.
  void set_timeout(uint32_t timeout_ms) { this->timeout_ms_ = timeout_ms; }
  uint32_t get_timeout() const { return this->timeout_ms_; }

  /// Record that the device was heard from, carrying the status it reported for itself.
  void mark_seen(MasterbusDeviceStatus status);
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
  uint32_t timeout_ms_{0};
  uint32_t last_seen_{0};
  MasterbusDeviceStatus status_{MasterbusDeviceStatus::MASTERBUS_DEVICE_STATUS_OFFLINE};
};

#ifdef USE_MASTERBUS_SCAN
/// One device the scan heard announce itself. Held as data rather than log text so that a test can
/// assert on what was discovered instead of on the wording of a log line.
struct MasterbusDiscoveredDevice {
  uint32_t address;
  uint32_t announcements;
};
#endif

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

  canbus::Canbus *get_canbus() const { return this->canbus_; }

  /// Hand the hub a frame from the bus. Registered as the CAN receive callback in setup(); public
  /// because the frame boundary is the seam this component is exercised through.
  void on_frame(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data);

  /// Ask a device for one field's current value. Returns whether the request reached the bus.
  bool request_field(const MasterbusEntity &entity);

#ifdef USE_MASTERBUS_SCAN
  /// Every device heard announcing itself since boot, in the order they were first heard.
  const StaticVector<MasterbusDiscoveredDevice, MASTERBUS_SCAN_MAX_DEVICES> &get_discovered_devices() const {
    return this->discovered_;
  }
  /// Ask every device on the bus to announce itself. This is the one thing a scan transmits.
  bool request_nodes();
  /// Write the discovered devices out as configuration the user can paste.
  void report_scan();
#endif

  /// Ask a device to set one of its boolean fields. Returns whether the request reached the bus.
  bool write_boolean(const MasterbusEntity &entity, bool state);

#ifdef MASTERBUS_DEVICE_COUNT
  void register_device(MasterbusDevice *device) { this->devices_.push_back(device); }
#endif
#ifdef MASTERBUS_ENTITY_COUNT
  void register_entity(MasterbusEntity *entity) { this->entities_.push_back(entity); }
  /// Report every entity of one device as unavailable, in one go.
  void publish_device_unavailable(const MasterbusDevice *device);
#endif

 protected:
#ifdef MASTERBUS_DEVICE_COUNT
  /// Drop devices that have gone silent past their timeout.
  void check_availability_();
  /// The device this identifier belongs to, or nullptr when it is not one we were told about.
  MasterbusDevice *find_device_(uint32_t address);
#endif
#ifdef MASTERBUS_ENTITY_COUNT
  /// Hand a decoded value to every entity configured for that field.
  void publish_value_(const MasterbusDevice *device, MasterbusTab tab, uint16_t param, float value);
#endif
#ifdef USE_MASTERBUS_LOG_ALL_FRAMES
  void log_frame_(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data);
#endif

#ifdef USE_MASTERBUS_SCAN
  /// Note that this address announced itself. Nothing is transmitted: devices announce unprompted.
  void record_announcement_(uint32_t address);
#endif

  canbus::Canbus *canbus_;
#ifdef USE_MASTERBUS_SCAN
  StaticVector<MasterbusDiscoveredDevice, MASTERBUS_SCAN_MAX_DEVICES> discovered_;
#endif
#ifdef MASTERBUS_DEVICE_COUNT
  StaticVector<MasterbusDevice *, MASTERBUS_DEVICE_COUNT> devices_;
#endif
#ifdef MASTERBUS_ENTITY_COUNT
  StaticVector<MasterbusEntity *, MASTERBUS_ENTITY_COUNT> entities_;
#endif
};

}  // namespace esphome::masterbus
