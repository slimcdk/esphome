#pragma once

#include "esphome/components/usb_pd_sink/usb_pd_sink.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
// #include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace usb_pd_sink {

#define LOG_USB_PD_SINK(this) \
  ESP_LOGCONFIG(TAG, "  Voltage: %d mV", this->milli_voltage_); \
  ESP_LOGCONFIG(TAG, "  Ampere: %d mA", this->milli_ampere_); \
  // LOG_SENSOR("  ", "Voltage Sensor", this->voltage_sensor_); \
  // LOG_SENSOR("  ", "Ampere Sensor", this->ampere_sensor_);



class UsbPdSink {
 public:

  void set_milli_voltage(uint16_t mV) { this->milli_voltage_ = mV; }
  void set_milli_ampere(uint16_t mA) { this->milli_ampere_ = mA; }

  // void set_voltage_sensor(sensor::Sensor *voltage_sensor) { this->voltage_sensor_ = voltage_sensor; }
  // void set_ampere_sensor(sensor::Sensor *ampere_sensor) { this->ampere_sensor_ = ampere_sensor; }
  // void publish_voltage_state();
  // void publish_ampere_state();

  uint16_t milli_voltage() { return this->milli_voltage_; }
  uint16_t milli_ampere() { return this->milli_ampere_; }
  bool has_connection() { return this->has_connection_; }
  
  virtual void negotiate() {}

 protected:

  void received_voltage_capabilities();
  void received_ampere_capabilities();

  uint16_t milli_voltage_{5000}, milli_ampere_{3000};
  uint16_t source_milli_voltage_{5000}, source_milli_ampere_{3000};
  bool has_connection_{false};

  // sensor::Sensor *voltage_sensor_, *ampere_sensor_;
};

}  // namespace usb_pd_sink
}  // namespace esphome


