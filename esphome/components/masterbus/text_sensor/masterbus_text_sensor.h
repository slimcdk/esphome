#pragma once

#include "esphome/components/masterbus/masterbus.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::masterbus {

class MasterbusTextSensor : public text_sensor::TextSensor, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  /// The names of a list field's entries, in the device's own order. Codegen only: the table is
  /// static and outlives the sensor.
  void set_options(const char *const *options, size_t count) {
    this->options_ = options;
    this->option_count_ = count;
  }

  void publish_masterbus_value(const MasterbusValue &value) override;
  // A text sensor has no way to clear a published string, so the state flag is all that can be
  // dropped here.
  void publish_masterbus_unavailable() override { this->set_has_state(false); }

 protected:
  const char *const *options_{nullptr};
  size_t option_count_{0};
};

}  // namespace esphome::masterbus
