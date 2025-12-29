#include "tmc_hub.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tmc_hub {

static const char *const TAG = "tmc_hub";

void TMCHub::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TMC UART Hub...");
}

void TMCHub::dump_config() {
  ESP_LOGCONFIG(TAG, "TMC Hub:");
  ESP_LOGCONFIG(TAG, "  Drivers attached: %d", this->devices_in_hub_.size());
  for (auto &device : this->devices_in_hub_) {
    ESP_LOGCONFIG(TAG, "    - ID: %s, Address: 0x%02X", device.id.c_str(), device.address);
  }
}

uint8_t TMCHub::calculate_crc(uint8_t *datagram, uint8_t datagram_len) {
  // Trinamic uses polynomial 0x07 (x^8 + x^2 + x^1 + 1)
  return esphome::crc8(datagram, datagram_len, 0x07);
}

void TMCHub::flush_echo(uint8_t len) {
  // On a 1-wire UART bus, everything written is immediately read back as an echo.
  // We must remove these 'len' bytes from the RX buffer before attempting a real read.
  uint8_t dummy;
  for (uint8_t i = 0; i < len; i++) {
    if (this->read_byte(&dummy)) {
      // Echo byte cleared
    }
  }
}

bool TMCHub::write_register(uint8_t address, uint8_t reg, uint32_t value) {
  uint8_t data[8];
  data[0] = 0x05;                        // Sync byte
  data[1] = address;                     // Slave Address
  data[2] = reg | 0x80;                  // Register + Write Bit (MSB set)
  data[3] = (uint8_t)(value >> 24);      // MSB
  data[4] = (uint8_t)(value >> 16);
  data[5] = (uint8_t)(value >> 8);
  data[6] = (uint8_t)(value & 0xFF);     // LSB
  data[7] = this->calculate_crc(data, 7);

  this->write_array(data, 8);
  this->flush_echo(8);
  return true;
}

bool TMCHub::read_register(uint8_t address, uint8_t reg, uint32_t &value) {
  uint8_t request[4];
  request[0] = 0x05;                     // Sync byte
  request[1] = address;                  // Slave Address
  request[2] = reg & 0x7F;               // Register (Read = MSB clear)
  request[3] = this->calculate_crc(request, 3);

  this->write_array(request, 4);
  this->flush_echo(4);

  // The TMC responds with an 8-byte datagram
  uint8_t response[8];
  if (!this->read_array(response, 8)) {
    ESP_LOGW(TAG, "Read from 0x%02X failed: Timeout", address);
    return false;
  }

  // Validate response CRC
  uint8_t calc_crc = this->calculate_crc(response, 7);
  if (calc_crc != response[7]) {
    ESP_LOGW(TAG, "Read from 0x%02X failed: CRC mismatch (Expected 0x%02X, Got 0x%02X)",
             address, response[7], calc_crc);
    return false;
  }

  // Byte 1 of response is always 0xFF (Master Address)
  // Reassemble the 32-bit value
  value = ((uint32_t)response[3] << 24) |
          ((uint32_t)response[4] << 16) |
          ((uint32_t)response[5] << 8)  |
          ((uint32_t)response[6]);

  return true;
}

}  // namespace tmc_hub
}  // namespace esphome
