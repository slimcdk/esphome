#include "esphome/core/log.h"
#include "stusb4500.h"

namespace esphome {
namespace stusb4500 {

static const char *const TAG = "usb_pd_sink.stusb4500";


void STUSB4500Component::dump_config() {
    ESP_LOGCONFIG(TAG, "STUSB4500:");
    LOG_PIN("  Reset Pin: ", this->reset_pin_);
    LOG_PIN("  Alert Pin: ", this->alert_pin_);
    LOG_I2C_DEVICE(this);
    LOG_USB_PD_SINK(this);

    // Dump STUSB4500 PDO settings
    // this->stusb4500_->read();
    // for (int i = 0; i < 3; i++) {
        // float current = this->stusb4500_->getCurrent(i);
        // float voltage = this->stusb4500_->getCurrent(i);
        // uint8_t lower_voltage = this->stusb4500_->getLowerVoltageLimit(i);
        // uint8_t upper_voltage = this->stusb4500_->getUpperVoltageLimit(i);
        // ESP_LOGI(TAG, "PDO %d: Current: %.3fA", i);
        // ESP_LOGI(TAG, "PDO %d: Voltage: %.3fV. Lower: %dV. Upper: %dV", i, voltage, lower_voltage, upper_voltage);
    // }
    // ESP_LOGI(TAG, "Prioritized PDO: %d", this->stusb4500_->getPdoNumber());

    // Dump PD Source capabilities
    // ESP_LOGI(TAG, "getFlexCurrent: %f", this->stusb4500_->getFlexCurrent());
    // ESP_LOGI(TAG, "getExternalPower: %d", this->stusb4500_->getExternalPower());
    // ESP_LOGI(TAG, "getUsbCommCapable: %d", this->stusb4500_->getUsbCommCapable());
    // ESP_LOGI(TAG, "getPowerAbove5vOnly: %d", this->stusb4500_->getPowerAbove5vOnly());
    // ESP_LOGI(TAG, "getReqSrcCurrent: %d", this->stusb4500_->getReqSrcCurrent());
    // ESP_LOGI(TAG, "getConfigOkGpio: %d", this->stusb4500_->getConfigOkGpio());
    // ESP_LOGI(TAG, "getGpioCtrl: %d", this->stusb4500_->getGpioCtrl());
}

void STUSB4500Component::setup() {
    ESP_LOGCONFIG(TAG, "Setting up STUSB4500...");
    if (this->reset_pin_ != nullptr) this->reset_pin_->setup();
    if (this->alert_pin_ != nullptr) this->alert_pin_->setup();

    //this->stusb4500_ = new STUSB4500();
    if (!this->stusb4500_->begin(this->address_, this->parent_->get_wire())) {
        this->mark_failed();
        return;
    }

    // Start off by negotiating 
    this->negotiate();
}

void STUSB4500Component::loop() {
    uint8_t ext_power = this->stusb4500_->getExternalPower();
    if (this->has_connection_ != (bool) ext_power) {
        this->has_connection_ == (bool) ext_power;
        ESP_LOGCONFIG(TAG, "Has power supply? %b", this->has_connection_);
    }
}

void STUSB4500Component::reset() {
    if (this->reset_pin_ == nullptr) {
        this->stusb4500_->softReset();
    }
    else {
        this->reset_pin_->digital_write(LOW);
        delay(1);
        this->reset_pin_->digital_write(HIGH);
    }
}


void STUSB4500Component::negotiate() {

    ESP_LOGCONFIG(TAG, "Requesting %dmA @ %dmV", this->milli_ampere_, this->milli_voltage_);
    this->stusb4500_->setVoltage(3, this->milli_voltage_ / 1000);
    this->stusb4500_->setCurrent(3, this->milli_ampere_ / 1000);
    this->stusb4500_->setPdoNumber(3);

    // Reset STUSB4500 to initialize negotiation with source
    reset();
}


}  // namespace stusb4500
}  // namespace esphome
