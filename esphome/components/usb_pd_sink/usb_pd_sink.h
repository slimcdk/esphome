#pragma once
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/core/log.h"

namespace esphome {
namespace usb_pd_sink {

#define LOG_USB_PD_SINK(this) \
  ESP_LOGCONFIG(TAG, "  Voltage: %d mV", this->milli_voltage_); \
  ESP_LOGCONFIG(TAG, "  Ampere: %d mA", this->milli_ampere_);

class PowerObject {
 public:
  PowerObject() = default;
 protected:
  uint16_t milli_voltage_{5000};
  uint16_t milli_ampere_{0};
  uint32_t milli_wattage_{0};
};

class SourceCapabilities : PowerObject{};

class UsbPdSinkState {
 public:
  UsbPdSinkState() = default;
};


class UsbPdSink {
 public:
  UsbPdSink() = default;
  
  // Parameter settings
  void set_milli_voltage(uint16_t mV) { this->milli_voltage_ = mV; }
  void set_milli_ampere(uint16_t mA) { this->milli_ampere_ = mA; }
  uint16_t milli_voltage() { return this->milli_voltage_; }
  uint16_t milli_ampere() { return this->milli_ampere_; }

  virtual void negotiate() {};
  void has_source(bool present);
  bool has_source() { return this->has_source_; }

  // Action adders
  void add_on_negotiation_callback(std::function<void(uint16_t, uint16_t)> &&callback);
  void add_on_source_connected_callback(std::function<void()> &&callback);
  void add_on_source_disconnected_callback(std::function<void()> &&callback);

  uint32_t last_negotiation_{0};

 protected:
  bool should_renegotiate() { return (millis()-this->last_negotiation_) > 10000; /*10s*/ }
  void received_voltage_capabilities();
  void received_ampere_capabilities();

  PowerObject SinkPDOs[3];

  uint16_t milli_voltage_{5000};
  uint16_t milli_ampere_{1500};
  bool has_source_{false};

  // Action managers
  CallbackManager<void(uint16_t, uint16_t)> on_negotiation_callback;
  CallbackManager<void()> on_source_connected_callback;
  CallbackManager<void()> on_source_disconnected_callback;
};
 
}  // namespace usb_pd_sink
}  // namespace esphome


