#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/usb_pd_sink/usb_pd_sink.h"

namespace esphome {
namespace usb_pd_sink {


class OnNegotiationTrigger : public Trigger<uint16_t, uint16_t> {
 public:
  explicit OnNegotiationTrigger(UsbPdSink *parent) {
    parent->add_on_negotiation_callback([this, parent](uint16_t mV, uint16_t mA) {
      parent->last_negotiation_ = millis();
      this->trigger(mV, mA);
    });
  }
};


class OnSourceConnectedTrigger : public Trigger<> {
 public:
  explicit OnSourceConnectedTrigger(UsbPdSink *parent) {
    parent->add_on_source_connected_callback([this]() { this->trigger(); });
  }
};


class OnSourceDisconnectedTrigger : public Trigger<> {
 public:
  explicit OnSourceDisconnectedTrigger(UsbPdSink *parent) {
    parent->add_on_source_disconnected_callback([this]() { this->trigger(); });
  }
};


} // usb_pd_sink
} // esphome