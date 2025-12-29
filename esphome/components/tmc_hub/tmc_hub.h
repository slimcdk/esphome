#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/crc.h"
#include <vector>

namespace esphome {
namespace tmc_hub {

struct HubDevice {
  std::string id;
  uint8_t address;
};

class TMCHub : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  // Child registration
  void add_device_to_hub(std::string id, uint8_t address) {
    this->devices_in_hub_.push_back({id, address});
  }

  // High-level API for Driver Components
  bool write_register(uint8_t address, uint8_t reg, uint32_t value);
  bool read_register(uint8_t address, uint8_t reg, uint32_t &value);

 protected:
  // Centralized CRC calculation using ESPHome core
  uint8_t calculate_crc(uint8_t *datagram, uint8_t datagram_len);

  // Handles the 1-wire UART echo clearing
  void flush_echo(uint8_t len);

  std::vector<HubDevice> devices_in_hub_;
};

}  // namespace tmc_hub
}  // namespace esphome
