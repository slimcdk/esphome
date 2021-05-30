#include "as5601.h"
#include "esphome/core/log.h"

namespace esphome {
namespace as5601 {

static const char *TAG = "as5601";


void AS5601::dump_config() {
    ESP_LOGCONFIG(TAG, "AS5601:");
    ESP_LOGCONFIG(TAG, "  AB positions: %d", this->_ab_positions);
    ESP_LOGCONFIG(TAG, "  Zero position: %d", this->_zero_position);
    LOG_BINARY_SENSOR("  ", "Magnet Field Presence", this->presence_binarysensor);
    LOG_SENSOR("  ", "Magnet Field Orientation", this->orientation_sensor);
    LOG_SENSOR("  ", "Magnet Field Magnitude", this->magnitude_sensor);
}


void AS5601::setup() {
    ESP_LOGCONFIG(TAG, "Setting up AS5601...");

    auto status = this->read_status_byte();
    if (!status) {
        ESP_LOGE(TAG, "Unable to get status from AS5601");
        this->mark_failed();
    }
    
    // Write configuration to sensor
    this->write_ab_resolution(this->_ab_positions);
    this->write_zero_position(this->_zero_position);

    // Initial sensor publishing
    if (this->magnitude_sensor != nullptr) {
        uint data = this->magnitude();
        this->magnitude_sensor->publish_state(data);
    }
    if (this->orientation_sensor != nullptr) {
        uint data = this->angle();
        this->orientation_sensor->publish_state(data);
    }
}


void AS5601::loop() {
    if (this->presence_binarysensor != nullptr) {
        bool data = this->presence();
        this->presence_binarysensor->publish_state(data);
    }

    // Publish any changes to the magnitude sensor if it is enabled
    if (this->magnitude_sensor != nullptr) {
        uint data = this->magnitude();
        if (data != this->magnitude_sensor->get_raw_state()) this->magnitude_sensor->publish_state(data);
    }

    // Publish any changes to the orientation sensor if it is enabled
    if (this->orientation_sensor != nullptr) {
        uint data = this->angle();
        if (data != this->orientation_sensor->get_raw_state()) this->orientation_sensor->publish_state(data);
    }
}


}  // namespace as5601
}  // namespace esphome