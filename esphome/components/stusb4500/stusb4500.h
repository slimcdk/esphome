#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/usb_pd_sink/usb_pd_sink.h"

namespace esphome {
namespace stusb4500 {


class STUSB4500Component : public usb_pd_sink::UsbPdSink, public Component, public i2c::I2CDevice {
 public:
  STUSB4500Component() = default;

  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override; 

  void set_pdo(uint8_t index, uint8_t voltage, float amperage);

};


}  // namespace stusb4500
}  // namespace esphome
