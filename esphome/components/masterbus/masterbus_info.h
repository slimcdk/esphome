#pragma once

#include "esphome/core/defines.h"

#ifdef MASTERBUS_INFO_COUNT

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/helpers.h"

#include "masterbus_protocol.h"

#include <cinttypes>
#include <vector>

namespace esphome::masterbus {

class MasterbusDevice;
class MasterbusHub;

/// What a device says about itself and is read off the bus, numbered as the vendor's GeneralString
/// question numbers the four strings, then the firmware, which comes from another question.
enum class MasterbusDeviceInfo : uint8_t {
  MASTERBUS_DEVICE_INFO_ARTICLE_NUMBER = GENERAL_STRING_ARTICLE_NUMBER,
  MASTERBUS_DEVICE_INFO_SERIAL_NUMBER = GENERAL_STRING_SERIAL_NUMBER,
  MASTERBUS_DEVICE_INFO_PRODUCT_NAME = GENERAL_STRING_PRODUCT_NAME,
  MASTERBUS_DEVICE_INFO_REVISION_CODE = GENERAL_STRING_REVISION_CODE,
  MASTERBUS_DEVICE_INFO_FIRMWARE = 5,
};

/// A text sensor that shows one thing a device says about itself, and which thing.
struct MasterbusInfoSensor {
  MasterbusDevice *device;
  MasterbusDeviceInfo info;
  text_sensor::TextSensor *sensor;
};

/** Reads what devices say about themselves, and shows it on the text sensors configured for it.
 *
 * One device and one question at a time, like everything else on this bus: each step sends at most
 * one question, and its answer, or a second of silence, moves the read on to the next.
 */
class MasterbusInfoReader final {
 public:
  explicit MasterbusInfoReader(MasterbusHub *hub) : hub_(hub) {}

  void add_sensor(MasterbusDevice *device, MasterbusDeviceInfo info, text_sensor::TextSensor *sensor) {
    this->sensors_.push_back({device, info, sensor});
  }

  /// Ask the next question, or give up on one that went unanswered, at the given time. Returns
  /// whether there is more to do.
  bool step(uint32_t now);
  /// Offer a frame from a device. Returns whether it answered the question being asked.
  bool on_frame(uint8_t type, uint32_t address, const std::vector<uint8_t> &data);
  void dump_config();

 protected:
  /// Hand what was read to every sensor that shows it, or nullptr where the device has none.
  void publish_(const char *text);
  /// Move on to the next thing the device being read is asked about.
  void next_();

  MasterbusHub *hub_;
  StaticVector<MasterbusInfoSensor, MASTERBUS_INFO_COUNT> sensors_;
  /// The device being read, or nullptr; which of its facts; and where the question stands.
  MasterbusDevice *device_{nullptr};
  uint8_t item_{0};
  uint8_t processor_{0};
  MasterbusStringRead string_;
  bool have_string_{false};
  bool waiting_{false};
  uint32_t sent_at_{0};
  char text_[STRING_BUFFER_LENGTH]{};
};

}  // namespace esphome::masterbus

#endif
