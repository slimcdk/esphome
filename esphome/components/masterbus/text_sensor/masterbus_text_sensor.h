#pragma once

#include "esphome/components/masterbus/masterbus.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::masterbus {

class MasterbusTextSensor : public text_sensor::TextSensor, public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  void publish_masterbus_value(const MasterbusValue &value) override {
    switch (value.type) {
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT:
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME:
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE:
        this->publish_state(value.as_text);
        break;
      default:
        this->publish_masterbus_unavailable();
        break;
    }
  }
  // A text sensor has no way to clear a published string, so the state flag is all that can be
  // dropped here.
  void publish_masterbus_unavailable() override { this->set_has_state(false); }
};

}  // namespace esphome::masterbus
