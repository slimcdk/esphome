#include "tmc_hub.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace tmc_hub {

void TMCHub::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TMC Hub...");
  ESP_LOGCONFIG(TAG, "TMC Hub setup done.");
}

void TMCHub::dump_config() {
  ESP_LOGCONFIG(TAG, "TMC Hub:");
  ESP_LOGCONFIG(TAG, "  Drivers in hub (%d):", this->devices_in_hub_.size());

  for (auto &&device : this->devices_in_hub_) {
    if (device.address.has_value()) {
      ESP_LOGCONFIG(TAG, "    Driver with id '%s' on address 0x%02X", device.id.c_str(), device.address);
    } else {
      ESP_LOGCONFIG(TAG, "    Driver with id '%s'", device.id.c_str());
    }
  }
}

}  // namespace tmc_hub
}  // namespace esphome
