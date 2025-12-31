#include "tmc2225_hub.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace tmc2225_hub {

void TMC2225Hub::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TMC2225 Hub...");
  ESP_LOGCONFIG(TAG, "TMC2225 Hub setup done.");
}

void TMC2225Hub::dump_config() {
  ESP_LOGCONFIG(TAG, "TMC2225 Hub:");
  ESP_LOGCONFIG(TAG, "  Drivers in hub (%d):", this->devices_in_hub_.size());

  for (auto &&device : this->devices_in_hub_) {
    ESP_LOGCONFIG(TAG, "    Driver with id '%s' on address 0x%02X", device.id.c_str(), device.address);
  }
}

}  // namespace tmc2225_hub
}  // namespace esphome
