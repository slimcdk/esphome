#include "tmc2224_hub.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace tmc2224_hub {

void TMC2224Hub::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TMC2224 Hub...");
  ESP_LOGCONFIG(TAG, "TMC2224 Hub setup done.");
}

void TMC2224Hub::dump_config() {
  ESP_LOGCONFIG(TAG, "TMC2224 Hub:");
  ESP_LOGCONFIG(TAG, "  Drivers in hub (%d):", this->devices_in_hub_.size());

  for (auto &&device : this->devices_in_hub_) {
    ESP_LOGCONFIG(TAG, "    Driver with id '%s' on address 0x%02X", device.id.c_str(), device.address);
  }
}

}  // namespace tmc2224_hub
}  // namespace esphome
