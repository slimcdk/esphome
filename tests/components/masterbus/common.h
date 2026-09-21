#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "esphome/components/canbus/canbus.h"
#include "esphome/components/logger/logger.h"
#include "esphome/components/masterbus/masterbus.h"
#include "esphome/core/application.h"

namespace esphome::masterbus::testing {

// The seam is the CAN frame boundary. Canbus::send_message and read_message are already pure
// virtual, so a double deriving from it feeds frames in and records what went out without any
// new abstraction.
class RecordingCanbus : public canbus::Canbus {
 public:
  std::vector<canbus::CanFrame> sent;
  /// What the next send is answered with. A bus that is off, or whose queue is full, refuses.
  canbus::Error error{canbus::ERROR_OK};

  void clear() { this->sent.clear(); }

 protected:
  bool setup_internal() override { return true; }

  canbus::Error send_message(struct canbus::CanFrame *frame) override {
    if (this->error != canbus::ERROR_OK)
      return this->error;
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

  void publish_masterbus_value(const MasterbusValue &value) override {
    MasterbusValue recorded = value;
    // The text is only borrowed for the duration of the call, so a recorded value has to point at
    // a copy of its own, exactly as a real entity keeps one.
    switch (value.type) {
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_TEXT:
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_TIME:
      case MasterbusValueType::MASTERBUS_VALUE_TYPE_DATE:
        this->texts_.emplace_back(value.as_text != nullptr ? value.as_text : "");
        recorded.as_text = this->texts_.back().c_str();
        break;
      default:
        break;
    }
    this->values.push_back(recorded);
  }
  void publish_masterbus_unavailable() override { this->unavailable_count++; }

 protected:
  // A deque, because a reference to an element must survive the next one being added.
  std::deque<std::string> texts_;
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

/// Collects what the scan logged, with the log's own header and colouring taken off. Those lines
/// are what the converter on the documentation page reads, so they are a contract and are checked
/// exactly. Nothing else the component logs is.
class ScanLines {
 public:
  void clear() { this->lines_.clear(); }
  const std::vector<std::string> &lines() const { return this->lines_; }

  void take(const char *message, size_t length) {
    std::string line;
    for (size_t at = 0; at < length; at++) {
      if (message[at] == '\033') {
        while (at < length && message[at] != 'm')
          at++;
        continue;
      }
      line += message[at];
    }
    const size_t header = line.rfind("]: ");
    if (header != std::string::npos)
      line = line.substr(header + 3);
    // Only the lines the converter reads. What the scan says around them is wording.
    if (line.rfind("device ", 0) == 0 || line.rfind("group ", 0) == 0 || line.rfind("field ", 0) == 0)
      this->lines_.push_back(line);
  }

 private:
  std::vector<std::string> lines_;
};

/// One capture for the whole run: a log callback cannot be taken off again, so registering one
/// per test would leave the logger calling into objects that no longer exist.
inline ScanLines &scan_lines() {
  static ScanLines lines;
  static const bool registered = []() {
    if (logger::global_logger == nullptr)
      return false;
    logger::global_logger->add_log_callback(
        &lines, [](void *self, uint8_t, const char *tag, const char *message, size_t length) {
          if (strcmp(tag, "masterbus.scan") == 0)
            static_cast<ScanLines *>(self)->take(message, length);
        });
    return true;
  }();
  EXPECT_TRUE(registered) << "no logger to listen to";
  return lines;
}

/// A frame as log_all_frames prints it: "ext 0x086D56EA [6] 01:00:92:ED:D1:41".
struct LoggedFrame {
  uint32_t can_id;
  bool extended_id;
  bool rtr;
  std::vector<uint8_t> data;
};

/// Read back a line the component logged, so a frame recorded off a bus - including one a user
/// reports from equipment nobody here owns - goes into a test in the form it was seen in.
inline LoggedFrame logged_frame(const std::string &line) {
  LoggedFrame frame{};
  std::istringstream fields(line);
  std::string token;

  fields >> token;
  frame.extended_id = token == "ext";
  fields >> token;
  frame.can_id = static_cast<uint32_t>(std::strtoul(token.c_str(), nullptr, 16));
  fields >> token;
  if (token == "rtr") {
    frame.rtr = true;
    fields >> token;
  }
  const size_t declared = std::strtoul(token.c_str() + 1, nullptr, 10);

  std::string payload;
  fields >> payload;
  for (size_t at = 0; at < payload.size(); at += 3)
    frame.data.push_back(static_cast<uint8_t>(std::strtoul(payload.substr(at, 2).c_str(), nullptr, 16)));

  EXPECT_EQ(frame.data.size(), declared) << "the line says " << declared << " bytes: " << line;
  return frame;
}

inline uint32_t frame_id(uint8_t type, uint32_t address) {
  return (static_cast<uint32_t>(type) << MESSAGE_TYPE_SHIFT) | address;
}

}  // namespace esphome::masterbus::testing
