#include "masterbus_text_sensor.h"

#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

namespace esphome::masterbus {

ESPHOME_LOG_TAG(TAG, "masterbus.text_sensor");

void MasterbusTextSensor::publish_masterbus_value(const MasterbusValue &value) {
  switch (value.type) {
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT:
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME:
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE:
      this->publish_state(value.as_text);
      return;
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_LIST_OPTION:
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_DEVICE_ID:
    case MasterbusValueType::MASTERBUS_VALUE_TYPE_EVENTABLE:
      // The device does not check its own index against its list, so it is checked here: an entry
      // beyond the ones configured is shown as unknown rather than as somebody else's name.
      if (value.as_raw >= this->option_count_) {
        ESP_LOGW(TAG, "Field %u reported entry %" PRIu32 ", which is beyond the configured options", this->get_param(),
                 value.as_raw);
        this->publish_masterbus_unavailable();
        return;
      }
      this->publish_state(this->options_[value.as_raw]);
      return;
    default:
      this->publish_masterbus_unavailable();
      return;
  }
}

#ifdef MASTERBUS_INFO_COUNT
// What the top five bits of an address say a device is, in the vendor's own words. The classes the
// vendor names none for are empty.
PROGMEM_STRING_TABLE(MasterbusDeviceClasses, "", "", "inverter", "", "", "", "inverter/charger", "", "", "", "charger",
                     "generator", "DC source", "", "interface", "", "DC switching", "AC switching", "",
                     "alternator battery monitor", "display panel", "", "", "", "DC/DC converter", "transformer", "",
                     "battery monitor", "DC measurement", "AC measurement", "AC/DC measurement", "system overview");

void MasterbusInfoTextSensor::setup() {
  if (this->info_ != MasterbusDeviceInfo::MASTERBUS_DEVICE_INFO_CLASS)
    return;
  // Nothing to ask: the class is part of the address the configuration already gives.
  const uint8_t device_kind = device_class(this->device_->get_address());
  char name[MasterbusDeviceClasses::MAX_LENGTH + 1];
  ESPHOME_strncpy_P(name, reinterpret_cast<ESPHOME_PGM_P>(MasterbusDeviceClasses::get_progmem_str(device_kind, 0)),
                    sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
  if (name[0] == '\0')
    snprintf(name, sizeof(name), "class %u", static_cast<unsigned>(device_kind));
  this->publish_state(name);
}

void MasterbusInfoTextSensor::dump_config() {
  LOG_TEXT_SENSOR("", "MasterBus device information", this);
  ESP_LOGCONFIG(TAG, "  Device: 0x%06" PRIX32, this->device_->get_address());
}
#endif

}  // namespace esphome::masterbus
