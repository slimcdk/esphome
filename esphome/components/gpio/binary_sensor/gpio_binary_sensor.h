#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace gpio {


struct GPIOBinarySensorStore {
  ISRInternalGPIOPin *pin_;
  volatile bool pin_state_;
  static void gpio_intr(GPIOBinarySensorStore *arg);
};


class GPIOBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void set_pin(GPIOPin *pin) { pin_ = pin; }
  void set_use_interrupt(bool use) { use_interrupt_ = use; }
  // ========== INTERNAL METHODS ==========
  // (In most use cases you won't need these)
  /// Setup pin
  void setup() override;
  void dump_config() override;
  /// Hardware priority
  float get_setup_priority() const override;
  /// Check sensor
  void loop() override;

 protected:
  GPIOPin *pin_;
  bool use_interrupt_;
  GPIOBinarySensorStore store_;
};


// void ICACHE_RAM_ATTR Tx20ComponentStore::gpio_intr(Tx20ComponentStore *arg) {
//   arg->pin_state = arg->pin->digital_read();
//   const uint32_t now = micros();
//   if (!arg->start_time) {
//     // only detect a start if the bit is high
//     if (!arg->pin_state) {
//       return;
//     }
//     arg->buffer[arg->buffer_index] = 1;
//     arg->start_time = now;
//     arg->buffer_index++;
//     return;
//   }
//   const uint32_t delay = now - arg->start_time;
//   const uint8_t index = arg->buffer_index;

//   // first delay has to be ~2400
//   if (index == 1 && (delay > 3000 || delay < 2400)) {
//     arg->reset();
//     return;
//   }
//   // second delay has to be ~1200
//   if (index == 2 && (delay > 1500 || delay < 1200)) {
//     arg->reset();
//     return;
//   }
//   // third delay has to be ~2400
//   if (index == 3 && (delay > 3000 || delay < 2400)) {
//     arg->reset();
//     return;
//   }

//   if (arg->tx20_available || ((arg->spent_time + delay > TX20_MAX_TIME) && arg->start_time)) {
//     arg->tx20_available = true;
//     return;
//   }
//   if (index <= MAX_BUFFER_SIZE) {
//     arg->buffer[index] = delay;
//   }
//   arg->spent_time += delay;
//   arg->start_time = now;
//   arg->buffer_index++;
// }

}  // namespace gpio
}  // namespace esphome
