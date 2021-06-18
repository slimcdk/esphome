#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/usb_pd_sink/usb_pd_sink.h"

namespace esphome {
namespace usb_pd_sink {

#define LOG_USB_PD_SINK(this) \
  ESP_LOGCONFIG(TAG, "  Voltage: %.0f V", this->voltage_); \

class UsbPdSink {
 public:
  void set_voltage(uint8_t voltage) { this->voltage_ = voltage; }
  void set_amperage(uint8_t amperage) { this->amperage_ = amperage; }


 protected:
  uint8_t voltage_{5};
  float amperage_{5};
};

template<typename... Ts> class NegotiateAction : public Action<Ts...> {
 public:
  explicit NegotiateAction(UsbPdSink *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(uint8_t, voltage);
  TEMPLATABLE_VALUE(uint8_t, amperage);

  void play(Ts... x) override {
    if (this->voltage_.has_value()) {
      ESP_LOGW("usb sink", "voltage %d", this->voltage_.value(x...));
      this->parent_->set_voltage(this->voltage_.value(x...)); 
    }
    
    if (this->amperage_.has_value()) {
      ESP_LOGW("usb sink", "amperage %.f", this->amperage_.value(x...));
      this->parent_->set_amperage(this->amperage_.value(x...)); 
    }
  }

 protected:
  UsbPdSink *parent_;
};


}  // namespace usb_pd_sink
}  // namespace esphome
