#pragma once

#include "esphome/core/defines.h"

#ifdef USE_MASTERBUS_TEXT

#include "masterbus_protocol.h"

#include <cinttypes>
#include <vector>

namespace esphome::masterbus {

class MasterbusDevice;
class MasterbusEntity;
class MasterbusHub;

/** Reads the string table entry a text field answered with, and publishes the text to the field.
 *
 * A text field answers with the number of an entry, not with the text, so reading the entry is a
 * second exchange. One read runs at a time: a device answers one question at a time, and a text
 * field that has to wait simply asks again on its next poll.
 */
class MasterbusTextReader {
 public:
  explicit MasterbusTextReader(MasterbusHub *hub) : hub_(hub) {}

  /// Start reading entry `string_id` for `entity`, at the given time. A field that arrives while
  /// another read is running keeps the text it has.
  void begin(MasterbusEntity *entity, uint16_t string_id, uint32_t now);
  /// Offer a frame. Returns whether it belonged to the read in flight.
  bool on_frame(uint8_t type, uint32_t address, const std::vector<uint8_t> &data, uint32_t now);
  /// Drop the read in flight if this device owes it: the device has stopped answering, so nothing
  /// more is coming.
  void forget(const MasterbusDevice *device);

 protected:
  /// End the read in flight, publishing what it assembled or reporting it unavailable.
  void finish_(bool found);

  MasterbusHub *hub_;
  /// The field the read is for, or nullptr.
  MasterbusEntity *entity_{nullptr};
  uint32_t sent_at_{0};
  MasterbusStringRead string_;
  /// Where the string is assembled. It lives here rather than in a publish call because it arrives
  /// in chunks across several frames, and because an entity only ever borrows the text it is handed.
  char text_[STRING_BUFFER_LENGTH]{};
};

}  // namespace esphome::masterbus

#endif
