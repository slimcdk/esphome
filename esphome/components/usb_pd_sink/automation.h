#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/usb_pd_sink/usb_pd_sink.h"

namespace esphome {
namespace usb_pd_sink {

class OnVoltageChangeTrigger : public Trigger<float> {
 public:
  explicit OnVoltageChangeTrigger(UsbPdSink *parent) {
    //parent->add_on_state_callback([this](float value) { this->trigger(value); });
  }
};

class OnAmpereChangeTrigger : public Trigger<float> {
 public:
  explicit OnAmpereChangeTrigger(UsbPdSink *parent) {
    //parent->add_on_state_callback([this](float value) { this->trigger(value); });
  }
};

class OnConnectorConnectedTrigger : public Trigger<float> {
 public:
  explicit OnConnectorConnectedTrigger(UsbPdSink *parent) {
    //parent->add_on_state_callback([this](float value) { this->trigger(value); });
  }
};

class OnConnectorDisconnectedTrigger : public Trigger<float> {
 public:
  explicit OnConnectorDisconnectedTrigger(UsbPdSink *parent) {
    //parent->add_on_state_callback([this](float value) { this->trigger(value); });
  }
};


} // usb_pd_sink
} // esphome