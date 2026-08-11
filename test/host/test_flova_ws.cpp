#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <FlovaWs.h>

static_assert(sizeof(FlovaWs) <= 512, "FlovaWs fixed storage exceeded its budget");

static uint32_t nowMs = 0;
uint32_t millis() { return nowMs; }
unsigned long micros() { return nowMs * 1000UL; }
void delay(unsigned long milliseconds) { nowMs += static_cast<uint32_t>(milliseconds); }

class FakeClient : public Client {
 public:
  bool socket = true;
  uint8_t written[8192] = {};
  size_t writtenLength = 0;
  uint8_t incoming[8192] = {};
  size_t incomingLength = 0;
  size_t incomingOffset = 0;
  bool respondToHandshake = true;
  bool includeProtocol = true;
  bool includeLongHeader = false;
  bool responseAdded = false;
  size_t maximumWriteBytes = sizeof(written);
  size_t writeCalls = 0;

  bool connected() override { return socket; }
  int available() override {
    return static_cast<int>(incomingLength - incomingOffset);
  }
  int read() override {
    return incomingOffset < incomingLength ? incoming[incomingOffset++] : -1;
  }
  size_t write(const uint8_t* data, size_t length) override {
    ++writeCalls;
    assert(writtenLength + length <= sizeof(written));
    const size_t writtenNow = length < maximumWriteBytes ? length : maximumWriteBytes;
    assert(writtenNow > 0);
    memcpy(written + writtenLength, data, writtenNow);
    writtenLength += writtenNow;
    if (respondToHandshake && !responseAdded && writtenLength >= 4 &&
        hasSequence(written, writtenLength,
                    reinterpret_cast<const uint8_t*>("\r\n\r\n"), 4)) {
      responseAdded = true;
      const char response[] =
          "HTTP/1.1 101 Switching Protocols\r\n"
          "Upgrade: WebSocket\r\n"
          "Connection: keep-alive, Upgrade\r\n"
          "Sec-WebSocket-Accept: ";
      feed(reinterpret_cast<const uint8_t*>(response), sizeof(response) - 1);
      const char* key = strstr(reinterpret_cast<const char*>(written), "Sec-WebSocket-Key: ");
      assert(key != nullptr);
      key += strlen("Sec-WebSocket-Key: ");
      char keyText[25] = {};
      memcpy(keyText, key, 24);
      uint8_t digest[20] = {};
      sha1(reinterpret_cast<const uint8_t*>(keyText), 24,
           reinterpret_cast<const uint8_t*>("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"), 36,
           digest);
      char accept[29] = {};
      base64(digest, sizeof(digest), accept);
      feed(reinterpret_cast<const uint8_t*>(accept), strlen(accept));
      if (includeLongHeader) {
        static const char prefix[] = "\r\nX-Flova-Trace: ";
        static const char suffix[] = "\r\n";
        uint8_t longValue[180] = {};
        memset(longValue, 'x', sizeof(longValue));
        feed(reinterpret_cast<const uint8_t*>(prefix), sizeof(prefix) - 1);
        feed(longValue, sizeof(longValue));
        feed(reinterpret_cast<const uint8_t*>(suffix), sizeof(suffix) - 1);
      }
      if (includeProtocol) {
        static const char tail[] =
            "\r\nSec-WebSocket-Protocol: flova.cbor.v1\r\n\r\n";
        feed(reinterpret_cast<const uint8_t*>(tail), sizeof(tail) - 1);
      } else {
        static const char tail[] = "\r\n\r\n";
        feed(reinterpret_cast<const uint8_t*>(tail), sizeof(tail) - 1);
      }
    }
    return writtenNow;
  }

  void feed(const uint8_t* data, size_t length) {
    assert(incomingLength + length <= sizeof(incoming));
    memcpy(incoming + incomingLength, data, length);
    incomingLength += length;
  }

  void feedFrame(uint8_t opcode, bool fin, const uint8_t* data, size_t length) {
    assert(length <= 512);
    uint8_t frame[520] = {};
    frame[0] = static_cast<uint8_t>((fin ? 0x80 : 0) | opcode);
    size_t header = 2;
    if (length <= 125) {
      frame[1] = static_cast<uint8_t>(length);
    } else {
      frame[1] = 126;
      frame[2] = static_cast<uint8_t>(length >> 8);
      frame[3] = static_cast<uint8_t>(length);
      header = 4;
    }
    if (length) memcpy(frame + header, data, length);
    feed(frame, length + header);
  }

 private:
  static bool hasSequence(const uint8_t* input, size_t length,
                          const uint8_t* sequence, size_t sequenceLength) {
    if (sequenceLength > length) return false;
    for (size_t i = 0; i <= length - sequenceLength; ++i)
      if (memcmp(input + i, sequence, sequenceLength) == 0) return true;
    return false;
  }

  static uint32_t rotate(uint32_t value, uint8_t count) {
    return (value << count) | (value >> (32 - count));
  }

  static void sha1(const uint8_t* first, size_t firstLength,
                   const uint8_t* second, size_t secondLength, uint8_t output[20]) {
    uint8_t input[128] = {};
    assert(firstLength + secondLength < sizeof(input) - 9);
    memcpy(input, first, firstLength);
    memcpy(input + firstLength, second, secondLength);
    const size_t length = firstLength + secondLength;
    input[length] = 0x80;
    const size_t blocks = length + 1 > 56 ? 128 : 64;
    const uint64_t bits = static_cast<uint64_t>(length) * 8;
    for (uint8_t i = 0; i < 8; ++i) input[blocks - 8 + i] = static_cast<uint8_t>(bits >> (56 - i * 8));
    uint32_t h[5] = {0x67452301UL, 0xEFCDAB89UL, 0x98BADCFEUL,
                     0x10325476UL, 0xC3D2E1F0UL};
    for (size_t block = 0; block < blocks; block += 64) {
      uint32_t w[80] = {};
      for (uint8_t i = 0; i < 16; ++i)
        w[i] = (static_cast<uint32_t>(input[block + i * 4]) << 24) |
               (static_cast<uint32_t>(input[block + i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(input[block + i * 4 + 2]) << 8) |
               input[block + i * 4 + 3];
      for (uint8_t i = 16; i < 80; ++i) w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
      uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
      for (uint8_t i = 0; i < 80; ++i) {
        uint32_t f = 0, k = 0;
        if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999UL; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1UL; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCUL; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6UL; }
        const uint32_t next = rotate(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotate(b, 30); b = a; a = next;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (uint8_t i = 0; i < 5; ++i) {
      output[i * 4] = static_cast<uint8_t>(h[i] >> 24);
      output[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
      output[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
      output[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
  }

  static void base64(const uint8_t* input, size_t length, char output[29]) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out = 0;
    for (size_t i = 0; i < length; i += 3) {
      const size_t remaining = length - i;
      const uint32_t value = (static_cast<uint32_t>(input[i]) << 16) |
                             (remaining > 1 ? static_cast<uint32_t>(input[i + 1]) << 8 : 0) |
                             (remaining > 2 ? input[i + 2] : 0);
      output[out++] = alphabet[(value >> 18) & 63];
      output[out++] = alphabet[(value >> 12) & 63];
      output[out++] = remaining > 1 ? alphabet[(value >> 6) & 63] : '=';
      output[out++] = remaining > 2 ? alphabet[value & 63] : '=';
    }
    output[out] = 0;
  }
};

static void verifyHandshakeAndFrames() {
  FakeClient client;
  client.includeProtocol = false;
  client.maximumWriteBytes = 3;
  FlovaWs websocket(client);
  assert(websocket.handshake("engine.example", "/api/device-link"));
  assert(websocket.connected());
  assert(!websocket.handshake("engine.example", "/api/device-link"));
  assert(websocket.handshakeFailure() == FlovaWs::HandshakeFailure::InvalidState);
  assert(strstr(reinterpret_cast<const char*>(client.written),
                "Host: engine.example:443\r\n") != nullptr);
  assert(strstr(reinterpret_cast<const char*>(client.written),
                "Sec-WebSocket-Protocol:") == nullptr);

  FakeClient customPort;
  FlovaWs customPortWebsocket(customPort);
  assert(customPortWebsocket.handshake("engine.example", 8443, "/api/device-link"));
  assert(strstr(reinterpret_cast<const char*>(customPort.written),
                "Host: engine.example:8443\r\n") != nullptr);

  FakeClient longHeader;
  longHeader.includeProtocol = false;
  longHeader.includeLongHeader = true;
  FlovaWs longHeaderWebsocket(longHeader);
  assert(longHeaderWebsocket.handshake("engine.example", "/api/device-link"));

  const uint8_t payload[] = {0x01, 0x02, 0x03};
  client.feedFrame(0x2, false, payload, 2);
  client.feedFrame(0x9, true, reinterpret_cast<const uint8_t*>("ok"), 2);
  client.feedFrame(0x0, true, payload + 2, 1);
  uint8_t output[8] = {};
  int count = websocket.read(output, 1);
  assert(count == 1 && output[0] == 0x01 && !websocket.messageComplete());
  size_t outputLength = 1;
  while (!websocket.messageComplete()) {
    count = websocket.read(output + outputLength, sizeof(output) - outputLength);
    assert(count >= 0);
    outputLength += static_cast<size_t>(count);
  }
  assert(outputLength == 3 && output[1] == 0x02 && output[2] == 0x03);
  assert(websocket.messageComplete());

  bool sawPong = false;
  for (size_t i = 0; i + 2 < client.writtenLength; ++i)
    if ((client.written[i] & 0x0F) == 0xA) sawPong = true;
  assert(sawPong);

  const size_t before = client.writtenLength;
  assert(websocket.sendBinary(payload, sizeof(payload)));
  assert(client.writtenLength > before + 6);
  assert((client.written[before] & 0x0F) == 0x2);
  assert((client.written[before + 1] & 0x80) != 0);
  const size_t firstMask = before + 2;
  assert(websocket.sendBinary(payload, sizeof(payload)));
  const size_t secondFrame = client.writtenLength - (sizeof(payload) + 6);
  assert(memcmp(client.written + firstMask, client.written + secondFrame + 2, 4) != 0);

  uint8_t extended[126] = {};
  client.feedFrame(0x2, true, extended, sizeof(extended));
  uint8_t extendedOutput[126] = {};
  int firstExtended = websocket.read(extendedOutput, sizeof(extendedOutput));
  assert(firstExtended >= 0);
  size_t extendedLength = static_cast<size_t>(firstExtended);
  while (!websocket.messageComplete()) {
    const int part = websocket.read(extendedOutput + extendedLength,
                                    sizeof(extendedOutput) - extendedLength);
    assert(part >= 0);
    extendedLength += static_cast<size_t>(part);
  }
  assert(extendedLength == sizeof(extended));
}

static void verifyRejection() {
  FakeClient client;
  client.respondToHandshake = false;
  const char response[] = "HTTP/1.1 200 OK\r\n\r\n";
  client.feed(reinterpret_cast<const uint8_t*>(response), sizeof(response) - 1);
  FlovaWs websocket(client);
  assert(!websocket.handshake("engine.example", "/", "flova.cbor.v1"));
  assert(websocket.error() == FlovaWs::Error::Handshake);
  assert(websocket.handshakeStatus() == 200);
  assert(websocket.handshakeFailure() == FlovaWs::HandshakeFailure::UnexpectedStatus);

  FakeClient invalidAccept;
  invalidAccept.respondToHandshake = false;
  const char invalidAcceptResponse[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: invalid\r\n\r\n";
  invalidAccept.feed(reinterpret_cast<const uint8_t*>(invalidAcceptResponse),
                     sizeof(invalidAcceptResponse) - 1);
  FlovaWs invalidAcceptWebsocket(invalidAccept);
  assert(!invalidAcceptWebsocket.handshake("engine.example", "/"));
  assert(invalidAcceptWebsocket.handshakeFailure() ==
         FlovaWs::HandshakeFailure::InvalidAccept);

  FakeClient missingUpgrade;
  missingUpgrade.respondToHandshake = false;
  const char missingUpgradeResponse[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: invalid\r\n\r\n";
  missingUpgrade.feed(reinterpret_cast<const uint8_t*>(missingUpgradeResponse),
                      sizeof(missingUpgradeResponse) - 1);
  FlovaWs missingUpgradeWebsocket(missingUpgrade);
  assert(!missingUpgradeWebsocket.handshake("engine.example", "/"));
  assert(missingUpgradeWebsocket.handshakeFailure() ==
         FlovaWs::HandshakeFailure::MissingUpgrade);

  FakeClient invalidHost;
  FlovaWs invalidHostWebsocket(invalidHost);
  assert(!invalidHostWebsocket.handshake("engine.example\n", "/"));
  assert(invalidHostWebsocket.handshakeFailure() == FlovaWs::HandshakeFailure::InvalidRequest);

  FakeClient oversized;
  FlovaWs oversizedWs(oversized);
  assert(oversizedWs.handshake("engine.example", "/", "flova.cbor.v1"));
  const uint8_t tooLarge[] = {0x82, 0x7F, 0, 0, 0, 0, 0, 0, 0x02, 0x01};
  oversized.feed(tooLarge, sizeof(tooLarge));
  uint8_t output[8] = {};
  assert(oversizedWs.read(output, sizeof(output)) < 0);
  assert(oversizedWs.error() == FlovaWs::Error::MessageTooLarge);

  FakeClient masked;
  FlovaWs maskedWs(masked);
  assert(maskedWs.handshake("engine.example", "/", "flova.cbor.v1"));
  const uint8_t maskedFrame[] = {0x82, 0x81, 1, 2, 3, 4, 'x'};
  masked.feed(maskedFrame, sizeof(maskedFrame));
  assert(maskedWs.read(output, sizeof(output)) < 0);
  assert(maskedWs.error() == FlovaWs::Error::Protocol);

  FakeClient invalidCloseCode;
  FlovaWs invalidCloseCodeWs(invalidCloseCode);
  assert(invalidCloseCodeWs.handshake("engine.example", "/", "flova.cbor.v1"));
  const uint8_t invalidCloseCodeFrame[] = {0x88, 0x02, 0x03, 0xEC};
  invalidCloseCode.feed(invalidCloseCodeFrame, sizeof(invalidCloseCodeFrame));
  assert(invalidCloseCodeWs.read(output, sizeof(output)) < 0);
  assert(invalidCloseCodeWs.error() == FlovaWs::Error::Protocol);

  FakeClient invalidCloseReason;
  FlovaWs invalidCloseReasonWs(invalidCloseReason);
  assert(invalidCloseReasonWs.handshake("engine.example", "/", "flova.cbor.v1"));
  const uint8_t invalidCloseReasonFrame[] = {0x88, 0x04, 0x03, 0xE8, 0xC0, 0xAF};
  invalidCloseReason.feed(invalidCloseReasonFrame, sizeof(invalidCloseReasonFrame));
  assert(invalidCloseReasonWs.read(output, sizeof(output)) < 0);
  assert(invalidCloseReasonWs.error() == FlovaWs::Error::Protocol);
}

static void verifyReconnectCycles() {
  FakeClient client;
  client.includeProtocol = false;
  FlovaWs websocket(client);
  for (size_t cycle = 0; cycle < 10000; ++cycle) {
    client.socket = true;
    client.writtenLength = 0;
    client.incomingLength = 0;
    client.incomingOffset = 0;
    client.responseAdded = false;
    assert(websocket.handshake("engine.example", "/"));
    websocket.close();
    client.socket = false;
    assert(!websocket.connected());
  }
}

static void verifyPeerCloseReconnect() {
  FakeClient client;
  client.includeProtocol = false;
  FlovaWs websocket(client);
  assert(websocket.handshake("engine.example", "/"));

  const uint8_t closeFrame[] = {0x88, 0x02, 0x03, 0xE8};
  client.feed(closeFrame, sizeof(closeFrame));
  uint8_t output[8] = {};
  assert(websocket.read(output, sizeof(output)) < 0);
  assert(!websocket.connected());
  websocket.close();

  client.socket = true;
  client.writtenLength = 0;
  client.incomingLength = 0;
  client.incomingOffset = 0;
  client.responseAdded = false;
  assert(websocket.handshake("engine.example", "/"));
  assert(websocket.connected());
}

static void verifyCoalescedWrite() {
  FakeClient client;
  client.includeProtocol = false;
  FlovaWs websocket(client);
  assert(websocket.handshake("engine.example", "/"));
  client.writtenLength = 0;
  client.writeCalls = 0;

  uint8_t workspace[FlovaWs::kMaximumMessageBytes +
                    FlovaWs::kMaximumOutgoingHeaderBytes] = {};
  uint8_t* payload = workspace + FlovaWs::kMaximumOutgoingHeaderBytes;
  const uint8_t expected[] = {0x01, 0x02, 0x03, 0x04};
  memcpy(payload, expected, sizeof(expected));

  size_t preparedLength = 0;
  assert(websocket.prepareBinary(payload, sizeof(expected), workspace,
                                 sizeof(workspace), preparedLength));
  assert(client.writeCalls == 0);
  assert(preparedLength == sizeof(expected) + 6);
  assert(workspace[0] == 0x82 && (workspace[1] & 0x80) != 0);
  for (size_t i = 0; i < sizeof(expected); ++i)
    assert((workspace[6 + i] ^ workspace[2 + (i & 3)]) == expected[i]);

  memcpy(payload, expected, sizeof(expected));
  assert(websocket.sendBinaryCoalesced(payload, sizeof(expected), workspace,
                                       sizeof(workspace)));
  assert(client.writeCalls == 1);
  assert(client.writtenLength == sizeof(expected) + 6);
  assert(client.written[0] == 0x82);
  assert((client.written[1] & 0x80) != 0);
  const uint8_t* mask = client.written + 2;
  for (size_t i = 0; i < sizeof(expected); ++i)
    assert((client.written[6 + i] ^ mask[i & 3]) == expected[i]);
}

int main() {
  verifyHandshakeAndFrames();
  verifyRejection();
  verifyReconnectCycles();
  verifyPeerCloseReconnect();
  verifyCoalescedWrite();
  return 0;
}
