#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/usb_pd_sink/usb_pd_sink.h"

namespace esphome {
namespace usb_pd_sink {

static const char *const TAG = "usb_pd_sink.action";

template<typename... Ts> class NegotiateAction : public Action<Ts...> {
 public:
  explicit NegotiateAction(UsbPdSink *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(uint16_t, milli_voltage);
  TEMPLATABLE_VALUE(uint16_t, milli_ampere);

  void play(Ts... x) override {
    // Set parameters
    if (this->milli_voltage_.has_value()) this->parent_->set_milli_voltage(this->milli_voltage_.value(x...));
    if (this->milli_ampere_.has_value()) this->parent_->set_milli_ampere(this->milli_ampere_.value(x...));

    this->parent_->negotiate();
  }

 protected:
  UsbPdSink *parent_;
};


}  // namespace usb_pd_sink
}  // namespace esphome
