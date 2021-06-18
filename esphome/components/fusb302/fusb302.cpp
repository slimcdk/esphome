#include "fusb302.h"
#include "esphome/core/log.h"

namespace esphome {
namespace fusb302 {

static const char *const TAG = "fusb302";


void FUSB302Component::dump_config() {
    ESP_LOGCONFIG(TAG, "FUSB302:");
    LOG_USB_PD_SINK(this);
}

void FUSB302Component::setup() {
    ESP_LOGCONFIG(TAG, "Setting up FUSB302");
}


}  // namespace fusb302
}  // namespace esphome
