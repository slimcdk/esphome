#include "gpio_binary_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace gpio {

static const char *const TAG = "gpio.binary_sensor";

void GPIOBinarySensor::setup() {
  this->pin_->setup();
  const bool initial_state = this->pin_->digital_read();

  if (this->use_interrupt_) {
    this->store_.pin_ = this->pin_->to_isr();
    this->pin_->attach_interrupt(&GPIOBinarySensorStore::gpio_intr, &this->store_, FALLING);
    this->store_.pin_state_ = initial_state;
  }

  this->publish_initial_state(initial_state);
}

void GPIOBinarySensor::dump_config() {
  LOG_BINARY_SENSOR("", "GPIO Binary Sensor", this);
  LOG_PIN("  Pin: ", this->pin_);
  if (this->use_interrupt_)
    ESP_LOGCONFIG(TAG, "  Using Interrupt");
}

void GPIOBinarySensor::loop() {
  this->publish_state(use_interrupt_ ? store_.pin_state_ : pin_->digital_read());
}

float GPIOBinarySensor::get_setup_priority() const { return setup_priority::HARDWARE; }


void ICACHE_RAM_ATTR /*HOT*/ GPIOBinarySensorStore::gpio_intr(GPIOBinarySensorStore *arg) {
  arg->pin_state_ = arg->pin_->digital_read();
}

}  // namespace gpio
}  // namespace esphome
