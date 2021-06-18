#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/usb_pd_sink/usb_pd_sink.h"


namespace esphome {
namespace fusb302 {


class FUSB302Component :  public usb_pd_sink::UsbPdSink, public Component, public i2c::I2CDevice {
 public:
  FUSB302Component() = default;

  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override; 

};


}  // namespace fusb302
}  // namespace esphome
