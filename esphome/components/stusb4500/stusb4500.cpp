#include "stusb4500.h"
#include "esphome/core/log.h"

namespace esphome {
namespace stusb4500 {

static const char *const TAG = "stusb4500";


void STUSB4500Component::dump_config() {
    ESP_LOGCONFIG(TAG, "STUSB4500:");
    LOG_USB_PD_SINK(this);
}

void STUSB4500Component::setup() {
    ESP_LOGCONFIG(TAG, "Setting up STUSB4500");
    // usbpd.setPowerDefaultUSB();
}



void STUSB4500Component::set_pdo(uint8_t index, uint8_t voltage, float amperage) {
    return;
}

}  // namespace stusb4500
}  // namespace esphome
