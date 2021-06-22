#pragma once

#include "esphome/core/esphal.h"
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/usb_pd_sink/usb_pd_sink.h"
// #include "esphome/components/binary_sensor/binary_sensor.h"

#include <SparkFun_STUSB4500.h>


namespace esphome {
namespace stusb4500 {


class STUSB4500Component : public usb_pd_sink::UsbPdSink, public Component, public i2c::I2CDevice {
 public:
  STUSB4500Component() = default;
  
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override;
  void loop() override;

  // Config setup
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_alert_pin(GPIOPin *pin) { this->alert_pin_ = pin; }
  // void set_source_connectior_binary_sensor(binary_sensor::BinarySensor source_connection) { this->source_connection_ = source_connection; }

  void negotiate();

 protected:
  void reset();

  STUSB4500 *stusb4500_ = new STUSB4500();
  GPIOPin *reset_pin_, *alert_pin_;

  // binary_sensor::BinarySensor *source_connection_{nullptr};
};


}  // namespace stusb4500
}  // namespace esphome
