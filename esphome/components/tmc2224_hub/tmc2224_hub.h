#pragma once

#include <vector>

#include "esphome/components/uart/uart.h"

namespace esphome {
namespace tmc2224_hub {

static const char *const TAG = "tmc2224_hub";

struct HubDevice {
  std::string id;
  uint8_t address;
};

class TMC2224Hub : public Component, public uart::UARTDevice {
 public:
  float get_setup_priority() const override { return setup_priority::BUS; }
  void setup() override;
  void dump_config() override;

  void add_device_to_hub_(std::string id, uint8_t address) { this->devices_in_hub_.push_back({id, address}); }

 protected:
  std::vector<HubDevice> devices_in_hub_;
};

}  // namespace tmc2224_hub
}  // namespace esphome
