#pragma once

#include "esphome/components/masterbus/masterbus.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::masterbus {

class MasterbusTextSensor final : public text_sensor::TextSensor, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void dump_config() override;

  /// The names of a list field's entries, in the device's own order. Codegen only: the table is
  /// static, outlives the sensor, and is a PROGMEM array of pointers.
  void set_options(const char *const *options, size_t count) {
    this->options_ = options;
    this->option_count_ = count;
  }

  void publish_masterbus_text(const char *text) override { this->publish_state(text); }
  void publish_masterbus_index(uint32_t index) override;
  // A text sensor has no way to clear a published string, so the state flag is all that can be
  // dropped here.
  void publish_masterbus_unavailable() override { this->set_has_state(false); }

 protected:
  const char *const *options_{nullptr};
  size_t option_count_{0};
};

#ifdef USE_MASTERBUS_DEVICE_CLASS
/// What kind of equipment a device is. Its address already carries that, so nothing is read off the
/// bus: the sensor publishes it once, at setup.
class MasterbusClassTextSensor final : public text_sensor::TextSensor, public Component {
 public:
  explicit MasterbusClassTextSensor(MasterbusDevice *device) : masterbus_device_(device) {}

  void setup() override;
  void dump_config() override;

 protected:
  MasterbusDevice *masterbus_device_;
};
#endif

}  // namespace esphome::masterbus
