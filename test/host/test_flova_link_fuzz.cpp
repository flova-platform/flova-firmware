#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <FlovaLinkCbor.h>
#include <FlovaLinkCodec.h>

namespace {

uint32_t nextByte(uint32_t& state) {
  state = state * 1664525U + 1013904223U;
  return state >> 24;
}

void exercise(const uint8_t* bytes, size_t length) {
  flova::link::FrameView frame = {};
  const flova::link::FrameResult result =
      flova::link::decodeWebSocketBinaryMessage(bytes, length, frame);
  if (result == flova::link::FrameResult::Complete) {
    assert(frame.payloadLength <= flova::link::kMaximumPayloadBytes);
    (void)flova::link::validateCanonicalCbor(frame.payload, frame.payloadLength);
  }
}

void testBoundedDeterministicFuzz() {
  uint8_t input[flova::link::kMaximumFrameBytes] = {};
  uint32_t state = 0xC0DEC0DEU;

  for (size_t length = 0; length <= sizeof(input); ++length) {
    for (size_t i = 0; i < length; ++i) input[i] = static_cast<uint8_t>(nextByte(state));
    exercise(input, length);
  }

  for (size_t iteration = 0; iteration < 4096; ++iteration) {
    const size_t length = nextByte(state) % (sizeof(input) + 1);
    for (size_t i = 0; i < length; ++i) input[i] = static_cast<uint8_t>(nextByte(state));
    exercise(input, length);
  }

  const uint8_t malformed[] = {
      0x81, 0x18, 0x00, 0x9f, 0xff, 0xc0, 0x5f, 0xff, 0x7f};
  assert(!flova::link::validateCanonicalCbor(malformed, sizeof(malformed)));
}

}  // namespace

int main() {
  testBoundedDeterministicFuzz();
  return 0;
}
