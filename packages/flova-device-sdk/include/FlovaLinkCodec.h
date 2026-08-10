#pragma once

#include <stddef.h>
#include <stdint.h>

namespace flova {
namespace link {

// A Device Link WebSocket binary message contains exactly one complete frame.
// The header is transport metadata only; payload bytes are schema-specific CBOR.
static const size_t kHeaderBytes = 12;
static const size_t kMaximumFrameBytes = 512;
static const size_t kMaximumPayloadBytes = kMaximumFrameBytes - kHeaderBytes;
static const uint8_t kSupportedFlags = 0;

inline bool isKnownMessageType(uint8_t messageType) {
  switch (messageType) {
    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06:
    case 0x07: case 0x08: case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x20:
    case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
    case 0x27: case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c:
    case 0x7f:
      return true;
    default:
      return false;
  }
}

struct FrameView {
  uint8_t messageType;
  uint8_t flags;
  uint64_t messageId;
  const uint8_t* payload;
  uint16_t payloadLength;
};

enum class FrameResult : uint8_t { Complete, Invalid };

inline bool encodeFrameHeader(uint8_t* frame, size_t frameCapacity,
                              uint8_t messageType, uint8_t flags,
                              uint64_t messageId, size_t payloadLength) {
  if (!frame || !isKnownMessageType(messageType) || flags != kSupportedFlags ||
      payloadLength > kMaximumPayloadBytes ||
      frameCapacity < kHeaderBytes + payloadLength)
    return false;

  frame[0] = messageType;
  frame[1] = flags;
  frame[2] = static_cast<uint8_t>(payloadLength >> 8);
  frame[3] = static_cast<uint8_t>(payloadLength);
  for (uint8_t i = 0; i < 8; ++i)
    frame[4 + i] = static_cast<uint8_t>(messageId >> (56 - i * 8));
  return true;
}

inline FrameResult decodeWebSocketBinaryMessage(const uint8_t* message,
                                                 size_t messageLength,
                                                 FrameView& frame) {
  // WebSocket fragmentation is handled by the WebSocket implementation. Device
  // Link deliberately does not provide a streaming/multi-frame parser here.
  if (!message || messageLength < kHeaderBytes ||
      messageLength > kMaximumFrameBytes)
    return FrameResult::Invalid;
  if (!isKnownMessageType(message[0]) || message[1] != kSupportedFlags)
    return FrameResult::Invalid;

  const size_t payloadLength =
      (static_cast<size_t>(message[2]) << 8) | message[3];
  if (payloadLength > kMaximumPayloadBytes ||
      messageLength != kHeaderBytes + payloadLength)
    return FrameResult::Invalid;

  uint64_t messageId = 0;
  for (uint8_t i = 0; i < 8; ++i)
    messageId = (messageId << 8) | message[4 + i];

  frame.messageType = message[0];
  frame.flags = message[1];
  frame.messageId = messageId;
  frame.payload = message + kHeaderBytes;
  frame.payloadLength = static_cast<uint16_t>(payloadLength);
  return FrameResult::Complete;
}

}  // namespace link
}  // namespace flova
