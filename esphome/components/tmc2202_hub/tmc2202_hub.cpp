#include "tmc2202_hub.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace tmc2202_hub {

void TMC2202Hub::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TMC2202 Hub...");
  ESP_LOGCONFIG(TAG, "TMC2202 Hub setup done.");
}

void TMC2202Hub::dump_config() {
  ESP_LOGCONFIG(TAG, "TMC2202 Hub:");
  ESP_LOGCONFIG(TAG, "  Drivers in hub (%d):", this->devices_in_hub_.size());

  for (auto &&device : this->devices_in_hub_) {
    ESP_LOGCONFIG(TAG, "    Driver with id '%s' on address 0x%02X", device.id.c_str(), device.address);
  }
}

}  // namespace tmc2202_hub
}  // namespace esphome
