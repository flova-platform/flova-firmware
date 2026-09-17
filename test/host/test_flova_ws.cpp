#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <FlovaWs.h>
#include <adapters/ArduinoDeviceLink.h>
#include <FlovaRetryBackoff.h>

static_assert(sizeof(FlovaWs) <= 640, "FlovaWs fixed storage exceeded its budget");

static uint32_t nowMs = 0;
HardwareSerial Serial;
uint32_t millis() { return nowMs; }
unsigned long micros() { return nowMs * 1000UL; }
void delay(unsigned long milliseconds) { nowMs += static_cast<uint32_t>(milliseconds); }

class TestEntropy : public FlovaEntropySource {
 public:
  uint8_t byte() override { return next_++; }

 private:
  uint8_t next_ = 1;
};

static TestEntropy entropy;

class FakeClient : public FlovaLinkStream {
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

static bool handshake(FlovaWs& ws, FakeClient& client, const char* host,
                      uint16_t port, const char* path) {
  uint8_t request[526] = {};
  size_t length = 0;
  if (!ws.startHandshake(host, port, path, request, sizeof(request), length)) return false;
  size_t offset = 0;
  while (offset < length) offset += client.write(request + offset, length - offset);
  for (unsigned i = 0; i < 200; ++i) {
    const auto progress = ws.pollHandshake();
    if (progress != FlovaWs::HandshakeProgress::InProgress)
      return progress == FlovaWs::HandshakeProgress::Complete;
    nowMs += 100;
  }
  return false;
}
static bool handshake(FlovaWs& ws, FakeClient& client, const char* host,
                      const char* path, const char* = nullptr) {
  return handshake(ws, client, host, 443, path);
}

class FakePlatform : public FlovaArduinoPlatform {
 public:
  bool connected() override { return client.connected(); }
  int available() override { return client.available(); }
  int read() override { return client.read(); }
  bool linkClosed() const override { return !client.socket; }
  bool startLink(const char*, uint16_t) override { client.socket = true; return true; }
  FlovaLinkOpenStatus pollLink() override {
    if (failOpen) { client.socket = false; return FlovaLinkOpenStatus::Failed; }
    return client.socket ? FlovaLinkOpenStatus::Connected : FlovaLinkOpenStatus::Failed;
  }
  const char* linkError() const override { return "insufficient_tls_heap"; }
  void closeLink() override { client.socket = false; clearWrite(); }
  bool linkWriteBusy() const override { return writeOffset < writeLength; }
  bool submitLinkWrite(const uint8_t* data, size_t length) override {
    ++submitCalls;
    if (failBootstrapSubmit && submitCalls > 1) return false;
    if (linkWriteBusy() || !data || !length || !client.socket) return false;
    assert(length <= sizeof(writeData));
    memcpy(writeData, data, length);
    writeLength = length;
    writeOffset = 0;
    return true;
  }
  bool serviceLinkWrite() override {
    if (!linkWriteBusy()) return true;
    if (failBootstrapWrite && submitCalls > 1) return false;
    const size_t written = client.write(writeData + writeOffset, writeLength - writeOffset);
    if (!written) return false;
    writeOffset += written;
    if (!linkWriteBusy()) clearWrite();
    return true;
  }
  flova::OtaInstallResult installOta(const FlovaLinkOtaOffer&) override {
    return flova::OtaInstallResult::DownloadFailed;
  }

  FakeClient client;
  bool failBootstrapSubmit = false;
  bool failBootstrapWrite = false;
  bool failOpen = false;
  size_t submitCalls = 0;

 private:
  void clearWrite() { writeOffset = 0; writeLength = 0; }
  uint8_t writeData[526] = {};
  size_t writeOffset = 0;
  size_t writeLength = 0;
};

static void advanceBootstrap(ArduinoDeviceLink& link) {
  for (uint8_t i = 0; i < 16; ++i) link.loop();
}

template <typename T, typename Encoder>
static void feedLinkFrame(FakePlatform& platform, uint8_t type, uint64_t id,
                          const T& value, Encoder encode) {
  uint8_t frame[512] = {};
  size_t bytes = 0;
  assert(encode(frame + 12, 500, &value, &bytes) == 0);
  assert(flova::link::encodeFrameHeader(frame, sizeof(frame), type, 0, id, bytes));
  platform.client.feedFrame(2, true, frame, bytes + 12);
}

static unsigned bindingCallbacks;
static void acceptBinding(const FlovaLinkInboundMessage& message) {
  if (message.type != FlovaLinkMessageType::DatastreamBound) return;
  assert(message.body.datastreamBound.count == 64);
  for (unsigned i = 0; i < 64; ++i) assert(message.body.datastreamBound.ids[i] == i + 1);
  ++bindingCallbacks;
}

static void verifyBindingBatches() {
  // Include rejection after a successful batch, when the next response must
  // still be correlated with the outstanding request and earlier IDs.
  for (unsigned failure = 0; failure < 5; ++failure) {
    FakePlatform platform;
    ArduinoDeviceLink link(platform, entropy);
    char keys[64][49] = {};
    const char* names[64] = {};
    for (unsigned i = 0; i < 64; ++i) {
      memset(keys[i], 'x', 48);
      keys[i][0] = 'A' + i / 26;
      keys[i][1] = 'a' + i % 26;
      names[i] = keys[i];
    }
    assert(link.configure("wss://engine.example/api/device-link"));
    assert(link.setDatastreamKeys(names, 64));
    link.setConfigurationGeneration(7);
    link.setCallback(acceptBinding);
    bindingCallbacks = 0;
    assert(link.connect("00112233-4455-6677-8899-aabbccddeeff", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
    advanceBootstrap(link);
    platform.client.writtenLength = 0;
    struct auth_ok auth = {1700000000000ULL, 30000, 1};
    feedLinkFrame(platform, 2, 0, auth, cbor_encode_auth_ok);
    unsigned offset = 0;
    while (offset < 64) {
      advanceBootstrap(link);
      const uint8_t* wire = platform.client.written;
      assert(platform.client.writtenLength >= 8 && wire[0] == 0x82);
      size_t count = wire[1] & 127;
      size_t maskAt = 2;
      if (count == 126) { count = (wire[2] << 8) | wire[3]; maskAt = 4; }
      assert(count <= 512 && platform.client.writtenLength == maskAt + 4 + count);
      uint8_t frameBytes[512] = {};
      for (size_t i = 0; i < count; ++i) frameBytes[i] = wire[maskAt + 4 + i] ^ wire[maskAt + (i % 4)];
      flova::link::FrameView frame = {};
      assert(flova::link::decodeWebSocketBinaryMessage(frameBytes, count, frame) == flova::link::FrameResult::Complete);
      assert(frame.messageType == 9);
      struct datastream_bind request = {};
      size_t consumed = 0;
      assert(cbor_decode_datastream_bind(frame.payload, frame.payloadLength, &request, &consumed) == 0);
      const size_t batch = request.datastream_bind_binding_keys.datastream_binding_keys_tstr1_48_count;
      assert(batch == (64 - offset > 9 ? 9 : 64 - offset));
      for (size_t i = 0; i < batch; ++i) {
        const auto& key = request.datastream_bind_binding_keys.datastream_binding_keys_tstr1_48[i];
        assert(key.len == 48 && memcmp(key.value, names[offset + i], 48) == 0);
      }
      assert(bindingCallbacks == 0 && !link.connected());
      platform.client.writtenLength = 0;
      if (offset && failure == 4) {
        nowMs += 15001;
        link.loop();
        assert(platform.linkClosed());
        break;
      }
      struct datastream_bound reply = {};
      reply.datastream_bound_bound_generation = offset && failure == 2 ? 8 : 7;
      reply.datastream_bound_bound_ids.datastream_bound_ids_compact_id_m_count = batch;
      for (size_t i = 0; i < batch; ++i)
        reply.datastream_bound_bound_ids.datastream_bound_ids_compact_id_m[i] = offset + i + 1;
      if (offset && failure == 3) reply.datastream_bound_bound_ids.datastream_bound_ids_compact_id_m[0] = 1;
      feedLinkFrame(platform, 10, frame.messageId + (offset && failure == 1 ? 1 : 0), reply, cbor_encode_datastream_bound);
      advanceBootstrap(link);
      if (offset && failure) { assert(platform.linkClosed()); break; }
      offset += batch;
    }
    assert(bindingCallbacks == (failure ? 0U : 1U));
    assert(link.connected() == !failure);
  }
}

static void verifyBootstrapAuthenticationSend() {
  static const char token[] = "ttttttttttttttttttttttttttttttttttttttttttt";
  static const char secret[] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

  FakePlatform platform;
  ArduinoDeviceLink link(platform, entropy);
  assert(link.configure("wss://engine.example/api/device-link"));
  assert(link.connectBootstrap(token, "esp32-001122334455", "universal_esp32", secret));
  advanceBootstrap(link);
  assert(link.connected());
  bool sawBinary = false;
  for (size_t i = 0; i < platform.client.writtenLength; ++i)
    if (platform.client.written[i] == 0x82) sawBinary = true;
  assert(sawBinary);

  FakePlatform rejectedPlatform;
  rejectedPlatform.failBootstrapSubmit = true;
  ArduinoDeviceLink rejected(rejectedPlatform, entropy);
  assert(rejected.configure("wss://engine.example/api/device-link"));
  assert(rejected.connectBootstrap(token, "esp32-001122334455", "universal_esp32", secret));
  advanceBootstrap(rejected);
  char error[48] = {};
  assert(rejected.takeBootstrapError(error, sizeof(error)));
  assert(strcmp(error, "bootstrap_auth_submit_failed") == 0);

  FakePlatform openFailurePlatform;
  openFailurePlatform.failOpen = true;
  ArduinoDeviceLink openFailure(openFailurePlatform, entropy);
  assert(openFailure.configure("wss://engine.example/api/device-link"));
  assert(openFailure.connectBootstrap(token, "esp8266-001122334455",
                                      "universal_esp8266", secret));
  openFailure.loop();
  memset(error, 0, sizeof(error));
  assert(openFailure.takeBootstrapError(error, sizeof(error)));
  assert(strcmp(error, "insufficient_tls_heap") == 0);
}

static void verifyHandshakeAndFrames() {
  FakeClient client;
  client.includeProtocol = false;
  client.maximumWriteBytes = 3;
  FlovaWs websocket(client, entropy);
  assert(handshake(websocket, client, "engine.example", "/api/device-link"));
  assert(websocket.connected());
  assert(!handshake(websocket, client, "engine.example", "/api/device-link"));
  assert(websocket.handshakeFailure() == FlovaWs::HandshakeFailure::InvalidState);
  assert(strstr(reinterpret_cast<const char*>(client.written),
                "Host: engine.example:443\r\n") != nullptr);
  assert(strstr(reinterpret_cast<const char*>(client.written),
                "Sec-WebSocket-Protocol:") == nullptr);

  FakeClient customPort;
  FlovaWs customPortWebsocket(customPort, entropy);
  assert(handshake(customPortWebsocket, customPort, "engine.example", 8443, "/api/device-link"));
  assert(strstr(reinterpret_cast<const char*>(customPort.written),
                "Host: engine.example:8443\r\n") != nullptr);

  FakeClient longHeader;
  longHeader.includeProtocol = false;
  longHeader.includeLongHeader = true;
  FlovaWs longHeaderWebsocket(longHeader, entropy);
  assert(handshake(longHeaderWebsocket, longHeader, "engine.example", "/api/device-link"));

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
  uint8_t txWorkspace[526] = {};
  assert(websocket.sendBinaryCoalesced(payload, sizeof(payload), txWorkspace, sizeof(txWorkspace)));
  assert(client.writtenLength > before + 6);
  assert((client.written[before] & 0x0F) == 0x2);
  assert((client.written[before + 1] & 0x80) != 0);
  const size_t firstMask = before + 2;
  assert(websocket.sendBinaryCoalesced(payload, sizeof(payload), txWorkspace, sizeof(txWorkspace)));
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

static void verifyCooperativeHandshake() {
  nowMs = 0;
  FakeClient client;
  client.includeProtocol = false;
  FlovaWs websocket(client, entropy);
  uint8_t request[384] = {};
  size_t requestLength = 0;
  assert(websocket.startHandshake("engine.example", 443, "/api/device-link",
                                  request, sizeof(request), requestLength));
  assert(requestLength && client.write(request, requestLength) == requestLength);
  FlovaWs::HandshakeProgress progress = FlovaWs::HandshakeProgress::InProgress;
  uint8_t polls = 0;
  while (progress == FlovaWs::HandshakeProgress::InProgress && polls++ < 32)
    progress = websocket.pollHandshake();
  assert(progress == FlovaWs::HandshakeProgress::Complete);
  assert(websocket.connected());
  assert(polls > 1);  // Response parsing is intentionally bounded per poll.

  FakeClient timeoutClient;
  timeoutClient.respondToHandshake = false;
  FlovaWs timeoutWebsocket(timeoutClient, entropy);
  assert(timeoutWebsocket.startHandshake("engine.example", 443, "/",
                                         request, sizeof(request),
                                         requestLength));
  assert(timeoutClient.write(request, requestLength) == requestLength);
  assert(timeoutWebsocket.pollHandshake() ==
         FlovaWs::HandshakeProgress::InProgress);
  nowMs = 10001;
  assert(timeoutWebsocket.pollHandshake() ==
         FlovaWs::HandshakeProgress::Failed);
  assert(timeoutWebsocket.handshakeFailure() ==
         FlovaWs::HandshakeFailure::ResponseTimeout);
}

static void verifyRejection() {
  FakeClient client;
  client.respondToHandshake = false;
  const char response[] = "HTTP/1.1 200 OK\r\n\r\n";
  client.feed(reinterpret_cast<const uint8_t*>(response), sizeof(response) - 1);
  FlovaWs websocket(client, entropy);
  assert(!handshake(websocket, client, "engine.example", "/", "flova.cbor.v1"));
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
  FlovaWs invalidAcceptWebsocket(invalidAccept, entropy);
  assert(!handshake(invalidAcceptWebsocket, invalidAccept, "engine.example", "/"));
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
  FlovaWs missingUpgradeWebsocket(missingUpgrade, entropy);
  assert(!handshake(missingUpgradeWebsocket, missingUpgrade, "engine.example", "/"));
  assert(missingUpgradeWebsocket.handshakeFailure() ==
         FlovaWs::HandshakeFailure::MissingUpgrade);

  FakeClient invalidHost;
  FlovaWs invalidHostWebsocket(invalidHost, entropy);
  assert(!handshake(invalidHostWebsocket, invalidHost, "engine.example\n", "/"));
  assert(invalidHostWebsocket.handshakeFailure() == FlovaWs::HandshakeFailure::InvalidRequest);

  FakeClient oversized;
  FlovaWs oversizedWs(oversized, entropy);
  assert(handshake(oversizedWs, oversized, "engine.example", "/", "flova.cbor.v1"));
  const uint8_t tooLarge[] = {0x82, 0x7F, 0, 0, 0, 0, 0, 0, 0x02, 0x01};
  oversized.feed(tooLarge, sizeof(tooLarge));
  uint8_t output[8] = {};
  assert(oversizedWs.read(output, sizeof(output)) < 0);
  assert(oversizedWs.error() == FlovaWs::Error::MessageTooLarge);

  FakeClient masked;
  FlovaWs maskedWs(masked, entropy);
  assert(handshake(maskedWs, masked, "engine.example", "/", "flova.cbor.v1"));
  const uint8_t maskedFrame[] = {0x82, 0x81, 1, 2, 3, 4, 'x'};
  masked.feed(maskedFrame, sizeof(maskedFrame));
  assert(maskedWs.read(output, sizeof(output)) < 0);
  assert(maskedWs.error() == FlovaWs::Error::Protocol);

  FakeClient invalidCloseCode;
  FlovaWs invalidCloseCodeWs(invalidCloseCode, entropy);
  assert(handshake(invalidCloseCodeWs, invalidCloseCode, "engine.example", "/", "flova.cbor.v1"));
  const uint8_t invalidCloseCodeFrame[] = {0x88, 0x02, 0x03, 0xEC};
  invalidCloseCode.feed(invalidCloseCodeFrame, sizeof(invalidCloseCodeFrame));
  assert(invalidCloseCodeWs.read(output, sizeof(output)) < 0);
  assert(invalidCloseCodeWs.error() == FlovaWs::Error::Protocol);

  FakeClient invalidCloseReason;
  FlovaWs invalidCloseReasonWs(invalidCloseReason, entropy);
  assert(handshake(invalidCloseReasonWs, invalidCloseReason, "engine.example", "/", "flova.cbor.v1"));
  const uint8_t invalidCloseReasonFrame[] = {0x88, 0x04, 0x03, 0xE8, 0xC0, 0xAF};
  invalidCloseReason.feed(invalidCloseReasonFrame, sizeof(invalidCloseReasonFrame));
  assert(invalidCloseReasonWs.read(output, sizeof(output)) < 0);
  assert(invalidCloseReasonWs.error() == FlovaWs::Error::Protocol);
}

static void verifyReconnectCycles() {
  FakeClient client;
  client.includeProtocol = false;
  FlovaWs websocket(client, entropy);
  for (size_t cycle = 0; cycle < 10000; ++cycle) {
    client.socket = true;
    client.writtenLength = 0;
    client.incomingLength = 0;
    client.incomingOffset = 0;
    client.responseAdded = false;
    assert(handshake(websocket, client, "engine.example", "/"));
    websocket.close();
    client.socket = false;
    assert(!websocket.connected());
  }
}

static void verifyPeerCloseReconnect() {
  FakeClient client;
  client.includeProtocol = false;
  FlovaWs websocket(client, entropy);
  assert(handshake(websocket, client, "engine.example", "/"));

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
  assert(handshake(websocket, client, "engine.example", "/"));
  assert(websocket.connected());
}

static void verifyAbortDoesNotWriteCloseFrame() {
  FakeClient client;
  client.includeProtocol = false;
  FlovaWs websocket(client, entropy);
  assert(handshake(websocket, client, "engine.example", "/"));
  client.writtenLength = 0;
  websocket.abort();
  assert(client.writtenLength == 0);
  assert(!websocket.connected());
}

static void verifyCoalescedWrite() {
  FakeClient client;
  client.includeProtocol = false;
  FlovaWs websocket(client, entropy);
  assert(handshake(websocket, client, "engine.example", "/"));
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
  {
    flova::RetryBackoff retry;
    for (unsigned i = 0; i < 10000; ++i) {
      const uint32_t delay = retry.next(static_cast<uint8_t>(i));
      assert(delay >= 875 && delay <= 60000);
    }
    retry.reset();
    assert(retry.next(255) == 1000);
  }
  {
    FakePlatform platform;
    platform.client.includeProtocol = false;
    FlovaWs ws(platform, entropy);
    uint8_t wire[526] = {};
    size_t length = 0;
    assert(ws.startHandshake("engine.example", 443, "/", wire, sizeof(wire), length));
    assert(platform.submitLinkWrite(wire, length));
    assert(platform.serviceLinkWrite());
    while (ws.pollHandshake() == FlovaWs::HandshakeProgress::InProgress) {}
    platform.client.writtenLength = 0;
    platform.client.maximumWriteBytes = 1;
    const uint8_t data[] = {1, 2, 3, 4};
    assert(ws.prepareBinary(data, sizeof(data), wire, sizeof(wire), length));
    uint8_t expected[526] = {};
    memcpy(expected, wire, length);
    assert(platform.submitLinkWrite(wire, length));
    memset(wire, 0xEE, sizeof(wire)); // Submission must own its copy.
    assert(platform.serviceLinkWrite());
    const uint8_t ping[] = {9, 8, 7};
    platform.client.feedFrame(9, true, ping, sizeof(ping));
    uint8_t received[8] = {};
    assert(ws.read(received, sizeof(received)) == 0);
    assert(ws.controlPending());
    while (platform.linkWriteBusy()) assert(platform.serviceLinkWrite());
    assert(platform.client.writtenLength == length);
    assert(memcmp(platform.client.written, expected, length) == 0);
    assert(ws.serviceControl());
    while (platform.linkWriteBusy()) assert(platform.serviceLinkWrite());
    assert(platform.client.written[length] == 0x8A);
    assert(platform.client.writtenLength == length + sizeof(ping) + 6);
  }
  {
    FakePlatform platform;
    ArduinoDeviceLink link(platform, entropy);
    assert(link.configure("wss://engine.example/"));
    assert(link.connectBootstrap("ttttttttttttttttttttttttttttttttttttttttttt",
                                "esp32-test", "universal_esp32",
                                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
    advanceBootstrap(link);
    platform.client.maximumWriteBytes = 1;
    const size_t ackStart = platform.client.writtenLength;
    FlovaLinkConfigurationReport report = {};
    report.messageId = 17;
    report.generation = 2;
    report.status = FlovaLinkResultStatus::Ok;
    assert(link.publishConfigurationReport(report));
    assert(platform.linkWriteBusy());
    link.beginDrain();
    for (unsigned i = 0; i < 128 && !link.drainComplete(); ++i) link.loop();
    assert(link.drainComplete() && !link.drainFailed());
    const uint8_t* wire = platform.client.written + ackStart;
    assert(wire[0] == 0x82 && (wire[1] & 0x80));
    const size_t ackLength = wire[1] & 0x7f;
    assert(ackLength < 126);
    // The entire ACK precedes the WebSocket close, even with one-byte writes.
    assert(platform.client.writtenLength >= ackStart + 6 + ackLength + 6);
    assert(wire[6 + ackLength] == 0x88);
  }
  verifyHandshakeAndFrames();
  verifyBootstrapAuthenticationSend();
  verifyBindingBatches();
  verifyCooperativeHandshake();
  verifyRejection();
  verifyReconnectCycles();
  verifyPeerCloseReconnect();
  verifyAbortDoesNotWriteCloseFrame();
  verifyCoalescedWrite();
  return 0;
}
