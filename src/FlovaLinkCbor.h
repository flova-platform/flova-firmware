#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern "C" {
#include <flova_link_decode.h>
#include <flova_link_encode.h>
}

#include <FlovaLinkCodec.h>

namespace flova {
namespace link {

// zcbor-generated functions decode directly into fixed schema structures. The
// wrapper adds the protocol restrictions zcbor does not model in CDDL itself:
// valid UTF-8, no tags/indefinite values, depth <= 4, and byte-for-byte
// deterministic representation.
static const uint8_t kMaximumCborNestingDepth = 4;
static const size_t kMaximumCborArrayItems = 32;
static const size_t kMaximumCborMapEntries = 16;

enum class CborResult : uint8_t {
  Complete,
  Invalid,
  SchemaError,
  ScratchTooSmall,
  NonCanonical,
};

namespace detail {

inline bool validUtf8(const uint8_t* text, size_t length) {
  for (size_t i = 0; i < length;) {
    const uint8_t first = text[i++];
    if (first <= 0x7f) continue;

    uint32_t codepoint = 0;
    uint8_t continuation = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      codepoint = first & 0x1f;
      continuation = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      codepoint = first & 0x0f;
      continuation = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      codepoint = first & 0x07;
      continuation = 3;
    } else {
      return false;
    }

    if (continuation > length - i) return false;
    for (uint8_t part = 0; part < continuation; ++part) {
      const uint8_t next = text[i++];
      if ((next & 0xc0) != 0x80) return false;
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((continuation == 2 && codepoint < 0x800) ||
        (continuation == 3 && codepoint < 0x10000) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff)
      return false;
  }
  return true;
}

inline bool readArgument(const uint8_t*& cursor, const uint8_t* end,
                         uint8_t additional, uint64_t& value) {
  if (additional < 24) {
    value = additional;
    return true;
  }
  if (additional > 27) return false;

  const uint8_t bytes = static_cast<uint8_t>(1U << (additional - 24));
  if (static_cast<size_t>(end - cursor) < bytes) return false;

  value = 0;
  for (uint8_t i = 0; i < bytes; ++i) value = (value << 8) | *cursor++;
  const uint64_t smallestForWidth =
      bytes == 1 ? 24ULL : bytes == 2 ? 256ULL : bytes == 4 ? 65536ULL :
                                                     4294967296ULL;
  return value >= smallestForWidth;
}

struct EncodedKey {
  const uint8_t* data;
  size_t length;
};

inline bool walkCanonical(const uint8_t*& cursor, const uint8_t* end,
                          uint8_t depth) {
  if (cursor == end) return false;
  const uint8_t initial = *cursor++;
  const uint8_t major = initial >> 5;
  const uint8_t additional = initial & 0x1f;

  // Tags, indefinite values, and unassigned additional values are outside the
  // Flova Link profile. Floats are handled separately below.
  if (major == 6 || additional == 31) return false;

  if (major == 7) {
    if (additional == 20 || additional == 21) return true;
    if (additional == 26) {
      if (static_cast<size_t>(end - cursor) < 4) return false;
      cursor += 4;
      return true;
    }
    if (additional == 27) {
      if (static_cast<size_t>(end - cursor) < 8) return false;
      cursor += 8;
      return true;
    }
    return false;
  }

  uint64_t argument = 0;
  if (!readArgument(cursor, end, additional, argument)) return false;
  if (major <= 1) return true;

  if (major == 2 || major == 3) {
    if (argument > static_cast<uint64_t>(end - cursor)) return false;
    if (major == 3 && !validUtf8(cursor, static_cast<size_t>(argument)))
      return false;
    cursor += static_cast<size_t>(argument);
    return true;
  }

  if (depth >= kMaximumCborNestingDepth) return false;
  if (major == 4) {
    if (argument > kMaximumCborArrayItems) return false;
    for (uint64_t i = 0; i < argument; ++i)
      if (!walkCanonical(cursor, end, static_cast<uint8_t>(depth + 1))) return false;
    return true;
  }

  if (major != 5 || argument > kMaximumCborMapEntries) return false;
  EncodedKey keys[kMaximumCborMapEntries];
  for (uint64_t i = 0; i < argument; ++i) {
    const uint8_t* keyStart = cursor;
    if (!walkCanonical(cursor, end, static_cast<uint8_t>(depth + 1))) return false;
    const size_t keyLength = static_cast<size_t>(cursor - keyStart);
    for (uint64_t prior = 0; prior < i; ++prior)
      if (keys[prior].length == keyLength &&
          memcmp(keys[prior].data, keyStart, keyLength) == 0)
        return false;
    keys[i].data = keyStart;
    keys[i].length = keyLength;
    if (!walkCanonical(cursor, end, static_cast<uint8_t>(depth + 1))) return false;
  }
  return true;
}

}  // namespace detail

inline bool validateCanonicalCbor(const uint8_t* payload, size_t payloadLength) {
  if (!payload || !payloadLength || payloadLength > kMaximumPayloadBytes) return false;
  const uint8_t* cursor = payload;
  const uint8_t* const end = payload + payloadLength;
  return detail::walkCanonical(cursor, end, 0) && cursor == end;
}

template <typename Schema>
inline CborResult decodeCanonical(const uint8_t* payload, size_t payloadLength,
                                  Schema& result,
                                  int (*decoder)(const uint8_t*, size_t, Schema*, size_t*),
                                  int (*encoder)(uint8_t*, size_t, const Schema*, size_t*),
                                  uint8_t* scratch,
                                  size_t scratchCapacity) {
  if (!validateCanonicalCbor(payload, payloadLength)) return CborResult::Invalid;
  const size_t usableScratch = scratchCapacity > kMaximumPayloadBytes
                                   ? kMaximumPayloadBytes
                                   : scratchCapacity;
  if (!scratch || usableScratch < payloadLength)
    return CborResult::ScratchTooSmall;

  memset(&result, 0, sizeof(result));
  size_t consumed = 0;
  if (decoder(payload, payloadLength, &result, &consumed) != 0 ||
      consumed != payloadLength)
    return CborResult::SchemaError;

  size_t encodedLength = 0;
  if (encoder(scratch, usableScratch, &result, &encodedLength) != 0 ||
      encodedLength != payloadLength)
    return CborResult::NonCanonical;
  return memcmp(payload, scratch, payloadLength) == 0 ? CborResult::Complete
                                                       : CborResult::NonCanonical;
}

template <typename Schema>
inline CborResult encodeCanonical(uint8_t* payload, size_t payloadCapacity,
                                  const Schema& input,
                                  int (*encoder)(uint8_t*, size_t, const Schema*, size_t*),
                                  size_t& payloadLength) {
  payloadLength = 0;
  const size_t usableCapacity = payloadCapacity > kMaximumPayloadBytes
                                    ? kMaximumPayloadBytes
                                    : payloadCapacity;
  if (!payload || !usableCapacity) return CborResult::ScratchTooSmall;
  if (encoder(payload, usableCapacity, &input, &payloadLength) != 0 ||
      !payloadLength || payloadLength > kMaximumPayloadBytes ||
      !validateCanonicalCbor(payload, payloadLength)) {
    payloadLength = 0;
    return CborResult::SchemaError;
  }
  return CborResult::Complete;
}

}  // namespace link
}  // namespace flova
