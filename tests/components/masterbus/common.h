#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "esphome/components/canbus/canbus.h"
#include "esphome/components/masterbus/masterbus.h"

namespace esphome::masterbus::testing {

// The seam is the CAN frame boundary. Canbus::send_message and read_message are already pure
// virtual, so a double deriving from it feeds frames in and records what went out without any
// new abstraction.
class RecordingCanbus : public canbus::Canbus {
 public:
  std::vector<canbus::CanFrame> sent;

  void clear() { this->sent.clear(); }

 protected:
  bool setup_internal() override { return true; }

  canbus::Error send_message(struct canbus::CanFrame *frame) override {
    this->sent.push_back(*frame);
    return canbus::ERROR_OK;
  }

  canbus::Error read_message(struct canbus::CanFrame *frame) override { return canbus::ERROR_NOMSG; }
};

// An entity that records what the hub handed it. Using this rather than a real sensor keeps the
// test on the frame boundary, which is the seam the component is meant to be exercised through.
class RecordingEntity : public MasterbusEntity {
 public:
  using MasterbusEntity::MasterbusEntity;

  std::vector<MasterbusValue> values;
  int unavailable_count{0};

  void publish_masterbus_value(const MasterbusValue &value) override { this->values.push_back(value); }
  void publish_masterbus_unavailable() override { this->unavailable_count++; }
};

/// Build the payload of a monitoring answer the way a device does: field number little-endian,
/// then the four value bytes little-endian. The message type belongs to the identifier, which
/// frame_id() below builds.
inline std::vector<uint8_t> monitoring_answer(uint16_t param, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return {static_cast<uint8_t>(param & 0xFF),        static_cast<uint8_t>(param >> 8),
          static_cast<uint8_t>(bits & 0xFF),         static_cast<uint8_t>((bits >> 8) & 0xFF),
          static_cast<uint8_t>((bits >> 16) & 0xFF), static_cast<uint8_t>((bits >> 24) & 0xFF)};
}

inline uint32_t frame_id(uint8_t type, uint32_t address) {
  return (static_cast<uint32_t>(type) << MESSAGE_TYPE_SHIFT) | address;
}

}  // namespace esphome::masterbus::testing
