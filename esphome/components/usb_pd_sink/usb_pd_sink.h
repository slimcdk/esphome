#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/usb_pd_sink/usb_pd_sink.h"

namespace esphome {
namespace usb_pd_sink {

#define LOG_USB_PD_SINK(this) \
  ESP_LOGCONFIG(TAG, "  Voltage: %d mV", this->milli_voltage_); \
  ESP_LOGCONFIG(TAG, "  Ampere: %d mA", this->milli_ampere_); \



class UsbPdSink {
 public:

  void set_milli_voltage(uint16_t mV) { this->milli_voltage_ = mV; }
  void set_milli_ampere(uint16_t mA) { this->milli_ampere_ = mA; }

  bool has_connection() { return this->has_connection_; }
  
  virtual void negotiate() {}

 protected:
  uint16_t milli_voltage_{5000};
  uint16_t milli_ampere_{3000};
  bool has_connection_{false};
};

}  // namespace usb_pd_sink
}  // namespace esphome
