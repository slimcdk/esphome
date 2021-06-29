#include "usb_pd_sink.h"
#include "esphome/core/log.h"

namespace esphome {
namespace usb_pd_sink {

static const char *const TAG = "usb_pd_sink";


void UsbPdSink::has_source(bool present) {

    // Trigger actions
    if (this->has_source_ != present) {
        if (present) on_source_connected_callback.call();
        else on_source_disconnected_callback.call();
        this->has_source_ = present;
    }
}

void UsbPdSink::add_on_negotiation_callback(std::function<void(uint16_t, uint16_t)> &&callback) {
    this->on_negotiation_callback.add(std::move(callback));
}

void UsbPdSink::add_on_source_connected_callback(std::function<void()> &&callback) {
    this->on_source_connected_callback.add(std::move(callback));
}
void UsbPdSink::add_on_source_disconnected_callback(std::function<void()> &&callback) {
    this->on_source_disconnected_callback.add(std::move(callback));
}


}  // namespace usb_pd_sink
}  // namespace esphome