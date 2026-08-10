#include <assert.h>
#include <string.h>

#include <FlovaLinkCbor.h>
#include <flova_link_vectors.h>

namespace {

using flova::link::CborResult;
using flova::link::FrameResult;

void verifyEmpty(const uint8_t* payload, size_t payloadLength,
                 int (*decoder)(const uint8_t*, size_t, void*, size_t*),
                 int (*encoder)(uint8_t*, size_t, const void*, size_t*)) {
  uint8_t scratch[flova::link::kMaximumPayloadBytes] = {};
  size_t consumed = 0;
  size_t encodedLength = 0;
  assert(flova::link::validateCanonicalCbor(payload, payloadLength));
  assert(decoder(payload, payloadLength, nullptr, &consumed) == 0);
  assert(consumed == payloadLength);
  assert(encoder(scratch, sizeof(scratch), nullptr, &encodedLength) == 0);
  assert(encodedLength == payloadLength);
  assert(memcmp(payload, scratch, payloadLength) == 0);
}

#define VERIFY_SCHEMA(schema, decode, encode)                                      \
  do {                                                                               \
    struct schema decoded = {};                                                      \
    uint8_t scratch[flova::link::kMaximumPayloadBytes] = {};                        \
    assert(flova::link::decodeCanonical(                                             \
               frame.payload, frame.payloadLength, decoded, decode, encode, scratch, \
               sizeof(scratch)) == CborResult::Complete);                           \
  } while (false)

void verifyVector(const FlovaLinkVector& vector) {
  flova::link::FrameView frame = {};
  assert(flova::link::decodeWebSocketBinaryMessage(vector.frame, vector.frame_size,
                                                     frame) == FrameResult::Complete);
  assert(frame.messageType == vector.message_type);
  assert(frame.messageId == vector.message_id);
  assert(frame.payloadLength == vector.frame_size - flova::link::kHeaderBytes);

  switch (frame.messageType) {
    case 0x01: VERIFY_SCHEMA(auth, cbor_decode_auth, cbor_encode_auth); break;
    case 0x02: VERIFY_SCHEMA(auth_ok, cbor_decode_auth_ok, cbor_encode_auth_ok); break;
    case 0x03: VERIFY_SCHEMA(zcbor_string, cbor_decode_auth_error, cbor_encode_auth_error); break;
    case 0x04: verifyEmpty(frame.payload, frame.payloadLength, cbor_decode_ping, cbor_encode_ping); break;
    case 0x05: verifyEmpty(frame.payload, frame.payloadLength, cbor_decode_pong, cbor_encode_pong); break;
    case 0x06: VERIFY_SCHEMA(bootstrap_auth, cbor_decode_bootstrap_auth, cbor_encode_bootstrap_auth); break;
    case 0x07: VERIFY_SCHEMA(bootstrap_committed, cbor_decode_bootstrap_committed, cbor_encode_bootstrap_committed); break;
    case 0x08: VERIFY_SCHEMA(zcbor_string, cbor_decode_bootstrap_error, cbor_encode_bootstrap_error); break;
    case 0x10: VERIFY_SCHEMA(heartbeat, cbor_decode_heartbeat, cbor_encode_heartbeat); break;
    case 0x11: VERIFY_SCHEMA(state, cbor_decode_state, cbor_encode_state); break;
    case 0x12: VERIFY_SCHEMA(command_result_r, cbor_decode_command_result, cbor_encode_command_result); break;
    case 0x13: VERIFY_SCHEMA(config_reported, cbor_decode_config_reported, cbor_encode_config_reported); break;
    case 0x14: VERIFY_SCHEMA(ota_reported, cbor_decode_ota_reported, cbor_encode_ota_reported); break;
    case 0x15: VERIFY_SCHEMA(schedule_reported, cbor_decode_schedule_reported, cbor_encode_schedule_reported); break;
    case 0x16: VERIFY_SCHEMA(schedule_renew, cbor_decode_schedule_renew, cbor_encode_schedule_renew); break;
    case 0x17: VERIFY_SCHEMA(time_request, cbor_decode_time_request, cbor_encode_time_request); break;
    case 0x18: VERIFY_SCHEMA(config_ack, cbor_decode_config_ack, cbor_encode_config_ack); break;
    case 0x20: VERIFY_SCHEMA(command, cbor_decode_command, cbor_encode_command); break;
    case 0x21: verifyEmpty(frame.payload, frame.payloadLength, cbor_decode_ingestion_ack, cbor_encode_ingestion_ack); break;
    case 0x22: VERIFY_SCHEMA(config_desired, cbor_decode_config_desired, cbor_encode_config_desired); break;
    case 0x23: VERIFY_SCHEMA(ota_desired, cbor_decode_ota_desired, cbor_encode_ota_desired); break;
    case 0x24: VERIFY_SCHEMA(schedule_desired, cbor_decode_schedule_desired, cbor_encode_schedule_desired); break;
    case 0x25: VERIFY_SCHEMA(time_response, cbor_decode_time_response, cbor_encode_time_response); break;
    case 0x26: VERIFY_SCHEMA(flow_control, cbor_decode_flow_control, cbor_encode_flow_control); break;
    case 0x27: VERIFY_SCHEMA(zcbor_string, cbor_decode_message_rejected, cbor_encode_message_rejected); break;
    case 0x28: VERIFY_SCHEMA(config_begin, cbor_decode_config_begin, cbor_encode_config_begin); break;
    case 0x29: VERIFY_SCHEMA(config_record, cbor_decode_config_record, cbor_encode_config_record); break;
    case 0x2a: VERIFY_SCHEMA(config_end, cbor_decode_config_end, cbor_encode_config_end); break;
    case 0x2b: VERIFY_SCHEMA(schedule_record_message, cbor_decode_schedule_record_message, cbor_encode_schedule_record_message); break;
    case 0x2c: VERIFY_SCHEMA(schedule_end, cbor_decode_schedule_end, cbor_encode_schedule_end); break;
    case 0x7f: VERIFY_SCHEMA(zcbor_string, cbor_decode_error, cbor_encode_error); break;
    default: assert(false);
  }
}

void testFrameBounds() {
  uint8_t maximum[flova::link::kMaximumFrameBytes] = {};
  assert(flova::link::encodeFrameHeader(maximum, sizeof(maximum), 0x10, 0,
                                        0x0102030405060708ULL,
                                        flova::link::kMaximumPayloadBytes));
  flova::link::FrameView frame = {};
  assert(flova::link::decodeWebSocketBinaryMessage(maximum, sizeof(maximum), frame) ==
         FrameResult::Complete);
  assert(frame.flags == 0 && frame.payloadLength == flova::link::kMaximumPayloadBytes);

  uint8_t oversized[flova::link::kMaximumFrameBytes + 1] = {};
  assert(flova::link::decodeWebSocketBinaryMessage(oversized, sizeof(oversized), frame) ==
         FrameResult::Invalid);
  assert(!flova::link::encodeFrameHeader(maximum, sizeof(maximum), 0x10, 0, 1,
                                         flova::link::kMaximumPayloadBytes + 1));
  assert(!flova::link::encodeFrameHeader(maximum, sizeof(maximum), 0x10, 1, 1, 1));
  assert(!flova::link::encodeFrameHeader(maximum, sizeof(maximum), 0x09, 0, 1, 1));
  assert(flova::link::decodeWebSocketBinaryMessage(maximum, sizeof(maximum) - 1, frame) ==
         FrameResult::Invalid);

  uint8_t twoFrames[26] = {};
  assert(flova::link::encodeFrameHeader(twoFrames, sizeof(twoFrames), 0x04, 0, 1, 1));
  assert(flova::link::encodeFrameHeader(twoFrames + 13, sizeof(twoFrames) - 13, 0x04, 0,
                                        2, 1));
  assert(flova::link::decodeWebSocketBinaryMessage(twoFrames, sizeof(twoFrames), frame) ==
         FrameResult::Invalid);
  twoFrames[1] = 1;
  assert(flova::link::decodeWebSocketBinaryMessage(twoFrames, 13, frame) ==
         FrameResult::Invalid);
  twoFrames[1] = 0;
  twoFrames[0] = 0x09;
  assert(flova::link::decodeWebSocketBinaryMessage(twoFrames, 13, frame) ==
         FrameResult::Invalid);
}

void testCborRejections() {
  const FlovaLinkVector& auth = flova_link_vectors[0];
  flova::link::FrameView frame = {};
  assert(flova::link::decodeWebSocketBinaryMessage(auth.frame, auth.frame_size, frame) ==
         FrameResult::Complete);

  struct auth decoded = {};
  uint8_t scratch[flova::link::kMaximumPayloadBytes] = {};
  uint8_t malformed[flova::link::kMaximumPayloadBytes + 1] = {};

  memcpy(malformed, frame.payload, frame.payloadLength);
  malformed[frame.payloadLength] = 0;
  assert(flova::link::decodeCanonical(malformed, frame.payloadLength + 1, decoded,
                                      cbor_decode_auth, cbor_encode_auth, scratch,
                                      sizeof(scratch)) == CborResult::Invalid);

  // Non-minimal integer 0, indefinite arrays, and tags are rejected before a
  // schema decoder can inspect them.
  const uint8_t nonCanonicalInteger[] = {0x84, 0x18, 0x00, 0x00, 0x19, 0x02, 0x00, 0x00};
  struct auth_ok authOk = {};
  assert(flova::link::decodeCanonical(nonCanonicalInteger,
                                      sizeof(nonCanonicalInteger), authOk,
                                      cbor_decode_auth_ok, cbor_encode_auth_ok, scratch,
                                      sizeof(scratch)) == CborResult::Invalid);

  malformed[0] = 0x9f;
  malformed[frame.payloadLength] = 0xff;
  assert(!flova::link::validateCanonicalCbor(malformed, frame.payloadLength + 1));
  malformed[0] = 0xc0;
  assert(!flova::link::validateCanonicalCbor(malformed, frame.payloadLength));
}

}  // namespace

int main() {
  testFrameBounds();
  for (size_t i = 0; i < flova_link_vector_count; ++i) verifyVector(flova_link_vectors[i]);
  testCborRejections();
  return 0;
}
