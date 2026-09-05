#pragma once

#include <Arduino.h>
#include <Client.h>
#include <stdio.h>

class FlovaEntropySource {
 public:
  virtual ~FlovaEntropySource() {}
  virtual uint8_t byte() = 0;
};

// Small RFC 6455 client for the Flova Link. The transport is deliberately
// borrowed: TLS, certificates, time, socket cleanup, and reconnect policy
// belong to the caller. The client only owns fixed WebSocket parser storage.
class FlovaWs {
 public:
  static const size_t kMaximumMessageBytes = 512;
  static const size_t kMaximumOutgoingHeaderBytes = 14;
  static const size_t kMaximumHeaderLineBytes = 128;
  static const size_t kMaximumDiscardedHeaderBytes = 512;
  static const size_t kMaximumControlBytes = 125;

  enum class Error : uint8_t {
    None,
    NotConnected,
    Handshake,
    Protocol,
    MessageTooLarge,
    Transport,
    PeerClosed
  };

  enum class HandshakeFailure : uint8_t {
    None,
    InvalidRequest,
    InvalidState,
    TransportNotConnected,
    RequestWrite,
    ResponseClosed,
    ResponseTimeout,
    HeaderTooLong,
    TooManyHeaders,
    InvalidStatus,
    UnexpectedStatus,
    MissingUpgrade,
    MissingConnection,
    MissingAccept,
    InvalidAccept,
    MissingSubprotocol,
    InvalidSubprotocol,
    UnexpectedExtension
  };

  enum class HandshakeProgress : uint8_t {
    InProgress,
    Complete,
    Failed
  };

  FlovaWs(Client& transport, FlovaEntropySource& entropy)
      : transport_(transport), entropy_(entropy) { reset(); }

  // Prepare the HTTP upgrade request in caller-owned bounded storage. The
  // owner sends it through its cooperative platform writer, then calls
  // pollHandshake() from the application loop until completion.
  bool startHandshake(const char* host, uint16_t port, const char* path,
                      uint8_t* output, size_t capacity, size_t& length) {
    length = 0;
    if (state_ != State::Closed) {
      handshakeFailure_ = HandshakeFailure::InvalidState;
      error_ = Error::Handshake;
      return false;
    }
    reset();
    if (!transport_.connected())
      return failHandshake(HandshakeFailure::TransportNotConnected,
                           Error::NotConnected);
    if (!output || !capacity || !port ||
        !validToken(host, sizeof(line_), false) || !validPath(path))
      return failHandshake(HandshakeFailure::InvalidRequest,
                           Error::NotConnected);

    uint8_t rawKey[16] = {};
    for (size_t i = 0; i < sizeof(rawKey); ++i) rawKey[i] = randomByte();
    if (!base64(rawKey, sizeof(rawKey), keyText_, sizeof(keyText_)))
      return failHandshake(HandshakeFailure::InvalidRequest);
    Sha1 sha;
    sha.update(reinterpret_cast<const uint8_t*>(keyText_), strlen(keyText_));
    sha.update(reinterpret_cast<const uint8_t*>(webSocketGuid()),
               strlen(webSocketGuid()));
    uint8_t digest[20] = {};
    sha.finish(digest);
    if (!base64(digest, sizeof(digest), acceptText_, sizeof(acceptText_)))
      return failHandshake(HandshakeFailure::InvalidRequest);

    const int written = snprintf(
        reinterpret_cast<char*>(output), capacity,
        "GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: Upgrade\r\n"
        "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: %s\r\nUser-Agent: flova-ws/1\r\n\r\n",
        path, host, static_cast<unsigned>(port), keyText_);
    if (written <= 0 || static_cast<size_t>(written) >= capacity)
      return failHandshake(HandshakeFailure::InvalidRequest);

    length = static_cast<size_t>(written);
    handshakeFirstLine_ = true;
    handshakeDeadline_ = millis() + 10000UL;
    state_ = State::Handshaking;
    return true;
  }

  HandshakeProgress pollHandshake() {
    if (state_ == State::Open) return HandshakeProgress::Complete;
    if (state_ != State::Handshaking) return HandshakeProgress::Failed;
    if (!transport_.connected()) {
      failHandshake(HandshakeFailure::ResponseClosed);
      return HandshakeProgress::Failed;
    }

    uint8_t processed = 0;
    while (processed < 64 && transport_.available() > 0) {
      const int input = transport_.read();
      if (input < 0) break;
      ++processed;
      const uint8_t byte = static_cast<uint8_t>(input);
      if (byte != '\n') {
        if (handshakeLineLength_ + 1 < sizeof(line_)) {
          line_[handshakeLineLength_++] = static_cast<char>(byte);
        } else {
          handshakeLineTruncated_ = true;
          if (++handshakeDiscarded_ > kMaximumDiscardedHeaderBytes) {
            failHandshake(HandshakeFailure::HeaderTooLong);
            return HandshakeProgress::Failed;
          }
        }
        continue;
      }

      if (handshakeLineLength_ && line_[handshakeLineLength_ - 1] == '\r')
        --handshakeLineLength_;
      line_[handshakeLineLength_] = 0;
      const bool complete = !line_[0];
      if (!acceptHandshakeLine()) return HandshakeProgress::Failed;
      handshakeLineLength_ = 0;
      handshakeDiscarded_ = 0;
      handshakeLineTruncated_ = false;
      line_[0] = 0;
      if (complete) return finishPolledHandshake();
    }

    if (static_cast<int32_t>(millis() - handshakeDeadline_) >= 0) {
      failHandshake(HandshakeFailure::ResponseTimeout);
      return HandshakeProgress::Failed;
    }
    return HandshakeProgress::InProgress;
  }

  // The Device Link server does not negotiate an application subprotocol;
  // binary framing and the first Link byte identify the protocol instead.
  // Keep optional negotiation for other callers, but accept a valid 101
  // response when no subprotocol was requested.
  bool handshake(const char* host, const char* path, const char* subprotocol = nullptr) {
    return handshake(host, 443, path, subprotocol);
  }

  bool handshake(const char* host, uint16_t port, const char* path,
                 const char* subprotocol = nullptr) {
    const bool hasSubprotocol = subprotocol && *subprotocol;
    if (state_ != State::Closed) {
      handshakeFailure_ = HandshakeFailure::InvalidState;
      error_ = Error::Handshake;
      return false;
    }
    reset();
    if (!transport_.connected())
      return failHandshake(HandshakeFailure::TransportNotConnected, Error::NotConnected);
    if (!port || !validToken(host, sizeof(line_), false) || !validPath(path) ||
        (hasSubprotocol && !validToken(subprotocol, sizeof(line_), false)))
      return failHandshake(HandshakeFailure::InvalidRequest, Error::NotConnected);
    state_ = State::Handshaking;

    uint8_t rawKey[16] = {};
    for (size_t i = 0; i < sizeof(rawKey); ++i) rawKey[i] = randomByte();
    if (!base64(rawKey, sizeof(rawKey), keyText_, sizeof(keyText_)))
      return failHandshake(HandshakeFailure::InvalidRequest);

    Sha1 sha;
    sha.update(reinterpret_cast<const uint8_t*>(keyText_), strlen(keyText_));
    sha.update(reinterpret_cast<const uint8_t*>(webSocketGuid()),
               strlen(webSocketGuid()));
    uint8_t digest[20] = {};
    sha.finish(digest);
    if (!base64(digest, sizeof(digest), acceptText_, sizeof(acceptText_)))
      return failHandshake(HandshakeFailure::InvalidRequest);

    if (!writeText("GET ") || !writeText(path) || !writeText(" HTTP/1.1\r\n") ||
        !writeText("Host: ") || !writeText(host) || !writeText(":") ||
        !writeUnsigned(port) || !writeText("\r\n") ||
        !writeText("Connection: Upgrade\r\nUpgrade: websocket\r\n") ||
        !writeText("Sec-WebSocket-Version: 13\r\n") ||
        !writeText("Sec-WebSocket-Key: ") || !writeText(keyText_) ||
        !writeText("\r\nUser-Agent: flova-ws/1\r\n") ||
        (hasSubprotocol && (!writeText("Sec-WebSocket-Protocol: ") ||
                            !writeText(subprotocol) || !writeText("\r\n"))) ||
        !writeText("\r\n"))
      return failHandshake(HandshakeFailure::RequestWrite, Error::Transport);

    bool upgrade = false;
    bool connection = false;
    bool acceptHeader = false;
    bool accept = false;
    bool protocol = !hasSubprotocol;
    bool protocolHeader = false;
    bool firstLine = true;
    bool extensions = false;
    uint8_t headerCount = 0;
    for (;;) {
      if (++headerCount > 32)
        return failHandshake(HandshakeFailure::TooManyHeaders);
      bool lineTruncated = false;
      const LineResult lineResult = readLine(line_, sizeof(line_), lineTruncated);
      if (lineResult != LineResult::Complete) {
        return failHandshake(
            lineResult == LineResult::Closed
                ? HandshakeFailure::ResponseClosed
                : lineResult == LineResult::Timeout
                      ? HandshakeFailure::ResponseTimeout
                      : HandshakeFailure::HeaderTooLong);
      }
      if (!line_[0]) break;
      if (lineTruncated) {
        if (firstLine) return failHandshake(HandshakeFailure::HeaderTooLong);
        // Ignore oversized nonessential headers after consuming the complete
        // line. Required headers remain valid only when their bounded value
        // can be parsed below; this prevents a large proxy cookie or tracing
        // header from consuming permanent board RAM.
        continue;
      }
      if (firstLine) {
        firstLine = false;
        if (!parseStatusLine(line_, handshakeStatus_))
          return failHandshake(HandshakeFailure::InvalidStatus);
        if (handshakeStatus_ != 101)
          return failHandshake(HandshakeFailure::UnexpectedStatus);
        continue;
      }
      char* colon = strchr(line_, ':');
      if (!colon) return failHandshake(HandshakeFailure::InvalidStatus);
      *colon = 0;
      char* value = colon + 1;
      while (*value == ' ' || *value == '\t') ++value;
      trim(value);
      if (equalsIgnoreCase(line_, "Upgrade")) {
        upgrade = hasToken(value, "websocket");
      } else if (equalsIgnoreCase(line_, "Connection")) {
        connection = hasToken(value, "upgrade");
      } else if (equalsIgnoreCase(line_, "Sec-WebSocket-Accept")) {
        acceptHeader = true;
        accept = strcmp(value, acceptText_) == 0;
      } else if (equalsIgnoreCase(line_, "Sec-WebSocket-Protocol")) {
        protocolHeader = true;
        protocol = hasSubprotocol && strcmp(value, subprotocol) == 0;
      } else if (equalsIgnoreCase(line_, "Sec-WebSocket-Extensions")) {
        extensions = *value != 0;
      }
    }
    if (firstLine) return failHandshake(HandshakeFailure::InvalidStatus);
    if (!upgrade) return failHandshake(HandshakeFailure::MissingUpgrade);
    if (!connection) return failHandshake(HandshakeFailure::MissingConnection);
    if (!acceptHeader) return failHandshake(HandshakeFailure::MissingAccept);
    if (!accept) return failHandshake(HandshakeFailure::InvalidAccept);
    if (hasSubprotocol && !protocol)
      return failHandshake(protocolHeader ? HandshakeFailure::InvalidSubprotocol
                                          : HandshakeFailure::MissingSubprotocol);
    if (extensions) return failHandshake(HandshakeFailure::UnexpectedExtension);

    state_ = State::Open;
    error_ = Error::None;
    memset(keyText_, 0, sizeof(keyText_));
    memset(acceptText_, 0, sizeof(acceptText_));
    memset(line_, 0, sizeof(line_));
    return true;
  }

  bool sendBinary(const uint8_t* data, size_t length) {
    if (state_ != State::Open || (!data && length)) return fail(Error::NotConnected);
    if (length > kMaximumMessageBytes) return fail(Error::MessageTooLarge);
    return sendFrame(0x2, data, length);
  }

  // The source may overlap the destination. This lets the caller provide a
  // fixed workspace with room for the client header and one transport write.
  bool sendBinaryCoalesced(uint8_t* data, size_t length, uint8_t* workspace,
                           size_t capacity) {
    size_t wireLength = 0;
    if (!prepareBinary(data, length, workspace, capacity, wireLength)) return false;
    return sendMeasured(workspace, wireLength);
  }

  // Prepare one complete masked client frame without touching the transport.
  // The caller may retain this fixed workspace and drain it cooperatively.
  bool prepareBinary(uint8_t* data, size_t length, uint8_t* workspace,
                     size_t capacity, size_t& wireLength) {
    wireLength = 0;
    if (state_ != State::Open || (!data && length) || !workspace ||
        length > kMaximumMessageBytes)
      return fail(Error::NotConnected);
    if (capacity < length + kMaximumOutgoingHeaderBytes)
      return fail(Error::MessageTooLarge);

    uint8_t header[14] = {};
    uint8_t mask[4] = {};
    const uint32_t maskWord = (static_cast<uint32_t>(randomByte()) << 24) |
                              (static_cast<uint32_t>(randomByte()) << 16) |
                              (static_cast<uint32_t>(randomByte()) << 8) |
                              randomByte();
    mask[0] = static_cast<uint8_t>(maskWord >> 24);
    mask[1] = static_cast<uint8_t>(maskWord >> 16);
    mask[2] = static_cast<uint8_t>(maskWord >> 8);
    mask[3] = static_cast<uint8_t>(maskWord);

    size_t headerLength = 2;
    header[0] = 0x82;
    header[1] = 0x80;
    if (length <= 125) {
      header[1] = static_cast<uint8_t>(header[1] | length);
    } else if (length <= 0xFFFF) {
      header[1] = 0xFE;
      header[2] = static_cast<uint8_t>(length >> 8);
      header[3] = static_cast<uint8_t>(length);
      headerLength = 4;
    } else {
      header[1] = 0xFF;
      for (uint8_t i = 0; i < 8; ++i)
        header[2 + i] = static_cast<uint8_t>(static_cast<uint64_t>(length) >> (56 - i * 8));
      headerLength = 10;
    }
    memcpy(header + headerLength, mask, sizeof(mask));
    headerLength += sizeof(mask);
    if (workspace + headerLength + length > workspace + capacity)
      return fail(Error::MessageTooLarge);
    memmove(workspace + headerLength, data, length);
    for (size_t i = 0; i < length; ++i)
      workspace[headerLength + i] ^= mask[i & 3];
    memcpy(workspace, header, headerLength);
    wireLength = headerLength + length;
    return true;
  }

  uint32_t lastSendDurationMs() const { return lastSendDurationMs_; }
  uint16_t lastSendWriteCalls() const { return lastSendWriteCalls_; }
  size_t lastSendWireBytes() const { return lastSendWireBytes_; }

  // Reads available bytes without blocking. The caller appends returned bytes
  // to the same message buffer until messageComplete() becomes true.
  int read(uint8_t* destination, size_t capacity) {
    if (state_ != State::Open) return -1;
    if (!transport_.connected()) return failRead(Error::Transport);
    if (!destination && capacity) return failRead(Error::Protocol);
    if (messageComplete_) {
      messageComplete_ = false;
      messageInProgress_ = false;
      messageLength_ = 0;
    }

    size_t written = 0;
    for (;;) {
      if (!frameActive_) {
        if (!readHeader()) return static_cast<int>(written);
        if (!startFrame()) return -1;
        if (!frameRemaining_) {
          finishFrame();
          if (messageComplete_ || written) return static_cast<int>(written);
          continue;
        }
      }

      if (frameControl_) {
        while (controlLength_ < frameLength_) {
          uint8_t byte = 0;
          if (!takeByte(byte)) return static_cast<int>(written);
          control_[controlLength_++] = byte;
          --frameRemaining_;
        }
        if (!finishControl()) return -1;
        continue;
      }

      if (written == capacity) return static_cast<int>(written);
      while (frameRemaining_ && written < capacity) {
        uint8_t byte = 0;
        if (!takeByte(byte)) return static_cast<int>(written);
        destination[written++] = byte;
        --frameRemaining_;
        ++messageLength_;
      }
      if (frameRemaining_) return static_cast<int>(written);
      finishFrame();
      if (messageComplete_ || written) return static_cast<int>(written);
    }
  }

  bool messageComplete() const { return messageComplete_; }

  bool ping() {
    if (state_ != State::Open) return false;
    return sendFrame(0x9, nullptr, 0);
  }

  void close() {
    if (state_ == State::Open) {
      if (!sendFrame(0x8, nullptr, 0)) {
        state_ = State::Closed;
        return;
      }
    }
    // The owner tears down the borrowed transport immediately after close().
    // There is therefore no read loop that can complete a peer close handshake;
    // leave the parser reusable for the next connection attempt.
    state_ = State::Closed;
  }

  void abort() { state_ = State::Closed; }

  bool connected() const { return state_ == State::Open && transport_.connected(); }
  Error error() const { return error_; }
  HandshakeFailure handshakeFailure() const { return handshakeFailure_; }
  uint16_t handshakeStatus() const { return handshakeStatus_; }

  static const char* handshakeFailureName(HandshakeFailure failure) {
    switch (failure) {
      case HandshakeFailure::None: return "none";
      case HandshakeFailure::InvalidRequest: return "invalid_request";
      case HandshakeFailure::InvalidState: return "invalid_state";
      case HandshakeFailure::TransportNotConnected: return "transport_not_connected";
      case HandshakeFailure::RequestWrite: return "request_write";
      case HandshakeFailure::ResponseClosed: return "response_closed";
      case HandshakeFailure::ResponseTimeout: return "response_timeout";
      case HandshakeFailure::HeaderTooLong: return "header_too_long";
      case HandshakeFailure::TooManyHeaders: return "too_many_headers";
      case HandshakeFailure::InvalidStatus: return "invalid_status";
      case HandshakeFailure::UnexpectedStatus: return "unexpected_status";
      case HandshakeFailure::MissingUpgrade: return "missing_upgrade";
      case HandshakeFailure::MissingConnection: return "missing_connection";
      case HandshakeFailure::MissingAccept: return "missing_accept";
      case HandshakeFailure::InvalidAccept: return "invalid_accept";
      case HandshakeFailure::MissingSubprotocol: return "missing_subprotocol";
      case HandshakeFailure::InvalidSubprotocol: return "invalid_subprotocol";
      case HandshakeFailure::UnexpectedExtension: return "unexpected_extension";
    }
    return "unknown";
  }

 private:
  enum class State : uint8_t { Closed, Handshaking, Open, Closing };
  enum class LineResult : uint8_t { Complete, Closed, Timeout, TooLong };

  struct Sha1 {
    uint32_t h[5] = {0x67452301UL, 0xEFCDAB89UL, 0x98BADCFEUL,
                     0x10325476UL, 0xC3D2E1F0UL};
    uint8_t block[64] = {};
    size_t used = 0;
    uint64_t bits = 0;

    static uint32_t rotate(uint32_t value, uint8_t count) {
      return (value << count) | (value >> (32 - count));
    }

    void process(const uint8_t* input) {
      uint32_t w[16] = {};
      uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
      for (uint8_t i = 0; i < 80; ++i) {
        const uint8_t slot = i & 15;
        if (i < 16) {
          w[slot] = (static_cast<uint32_t>(input[i * 4]) << 24) |
                    (static_cast<uint32_t>(input[i * 4 + 1]) << 16) |
                    (static_cast<uint32_t>(input[i * 4 + 2]) << 8) | input[i * 4 + 3];
        } else {
          const uint32_t expanded = w[(i - 3) & 15] ^ w[(i - 8) & 15] ^
                                     w[(i - 14) & 15] ^ w[(i - 16) & 15];
          w[slot] = rotate(expanded, 1);
        }
        uint32_t f = 0, k = 0;
        if (i < 20) {
          f = (b & c) | ((~b) & d);
          k = 0x5A827999UL;
        } else if (i < 40) {
          f = b ^ c ^ d;
          k = 0x6ED9EBA1UL;
        } else if (i < 60) {
          f = (b & c) | (b & d) | (c & d);
          k = 0x8F1BBCDCUL;
        } else {
          f = b ^ c ^ d;
          k = 0xCA62C1D6UL;
        }
        const uint32_t next = rotate(a, 5) + f + e + k + w[i & 15];
        e = d;
        d = c;
        c = rotate(b, 30);
        b = a;
        a = next;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    void update(const uint8_t* input, size_t length) {
      bits += static_cast<uint64_t>(length) * 8;
      while (length) {
        const size_t copy = (sizeof(block) - used < length) ? sizeof(block) - used : length;
        memcpy(block + used, input, copy);
        used += copy;
        input += copy;
        length -= copy;
        if (used == sizeof(block)) {
          process(block);
          used = 0;
        }
      }
    }

    void finish(uint8_t output[20]) {
      block[used++] = 0x80;
      if (used > 56) {
        while (used < sizeof(block)) block[used++] = 0;
        process(block);
        used = 0;
      }
      while (used < 56) block[used++] = 0;
      for (int8_t i = 7; i >= 0; --i) block[used++] = static_cast<uint8_t>(bits >> (i * 8));
      process(block);
      for (uint8_t i = 0; i < 5; ++i) {
        output[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        output[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        output[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        output[i * 4 + 3] = static_cast<uint8_t>(h[i]);
      }
    }
  };

  static const char* webSocketGuid() {
    return "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  }

  uint8_t randomByte() { return entropy_.byte(); }

  static bool base64(const uint8_t* input, size_t length, char* output, size_t capacity) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t needed = ((length + 2) / 3) * 4 + 1;
    if (!input || !output || capacity < needed) return false;
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
    return true;
  }

  static bool validToken(const char* value, size_t capacity, bool allowSlash) {
    if (!value || !*value || strnlen(value, capacity) >= capacity) return false;
    for (const char* cursor = value; *cursor; ++cursor) {
      const uint8_t character = static_cast<uint8_t>(*cursor);
      if (character < 0x20 || character == 0x7F ||
          (!allowSlash && *cursor == '/')) return false;
    }
    return true;
  }

  static bool validPath(const char* path) {
    if (!validToken(path, kMaximumHeaderLineBytes, true) || path[0] != '/') return false;
    return true;
  }

  static bool parseStatusLine(const char* line, uint16_t& status) {
    if (!line || strncmp(line, "HTTP/1.", 7) != 0 ||
        (line[7] != '0' && line[7] != '1') || line[8] != ' ' ||
        line[9] < '0' || line[9] > '9' || line[10] < '0' || line[10] > '9' ||
        line[11] < '0' || line[11] > '9' || line[12] != ' ') return false;
    status = static_cast<uint16_t>((line[9] - '0') * 100 +
                                   (line[10] - '0') * 10 + (line[11] - '0'));
    return true;
  }

  static bool equalsIgnoreCase(const char* left, const char* right) {
    while (*left && *right) {
      char a = *left++, b = *right++;
      if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
      if (a != b) return false;
    }
    return *left == 0 && *right == 0;
  }

  static void trim(char* value) {
    size_t length = strlen(value);
    while (length && (value[length - 1] == ' ' || value[length - 1] == '\t')) value[--length] = 0;
  }

  static bool hasToken(const char* value, const char* expected) {
    while (*value) {
      while (*value == ' ' || *value == '\t' || *value == ',') ++value;
      const char* start = value;
      while (*value && *value != ',' && *value != ' ' && *value != '\t') ++value;
      char token[32] = {};
      const size_t length = static_cast<size_t>(value - start);
      if (length >= sizeof(token)) return false;
      memcpy(token, start, length);
      if (equalsIgnoreCase(token, expected)) return true;
    }
    return false;
  }

  bool acceptHandshakeLine() {
    if (++handshakeHeaderCount_ > 32)
      return failHandshake(HandshakeFailure::TooManyHeaders);
    if (handshakeLineTruncated_) {
      if (handshakeFirstLine_)
        return failHandshake(HandshakeFailure::HeaderTooLong);
      return true;
    }
    if (!line_[0]) return true;
    if (handshakeFirstLine_) {
      handshakeFirstLine_ = false;
      if (!parseStatusLine(line_, handshakeStatus_))
        return failHandshake(HandshakeFailure::InvalidStatus);
      if (handshakeStatus_ != 101)
        return failHandshake(HandshakeFailure::UnexpectedStatus);
      return true;
    }

    char* colon = strchr(line_, ':');
    if (!colon) return failHandshake(HandshakeFailure::InvalidStatus);
    *colon = 0;
    char* value = colon + 1;
    while (*value == ' ' || *value == '\t') ++value;
    trim(value);
    if (equalsIgnoreCase(line_, "Upgrade"))
      handshakeUpgrade_ = hasToken(value, "websocket");
    else if (equalsIgnoreCase(line_, "Connection"))
      handshakeConnection_ = hasToken(value, "upgrade");
    else if (equalsIgnoreCase(line_, "Sec-WebSocket-Accept")) {
      handshakeAcceptHeader_ = true;
      handshakeAccept_ = strcmp(value, acceptText_) == 0;
    } else if (equalsIgnoreCase(line_, "Sec-WebSocket-Extensions"))
      handshakeExtensions_ = *value != 0;
    return true;
  }

  HandshakeProgress finishPolledHandshake() {
    if (handshakeFirstLine_)
      failHandshake(HandshakeFailure::InvalidStatus);
    else if (!handshakeUpgrade_)
      failHandshake(HandshakeFailure::MissingUpgrade);
    else if (!handshakeConnection_)
      failHandshake(HandshakeFailure::MissingConnection);
    else if (!handshakeAcceptHeader_)
      failHandshake(HandshakeFailure::MissingAccept);
    else if (!handshakeAccept_)
      failHandshake(HandshakeFailure::InvalidAccept);
    else if (handshakeExtensions_)
      failHandshake(HandshakeFailure::UnexpectedExtension);
    else {
      state_ = State::Open;
      error_ = Error::None;
      memset(keyText_, 0, sizeof(keyText_));
      memset(acceptText_, 0, sizeof(acceptText_));
      memset(line_, 0, sizeof(line_));
      return HandshakeProgress::Complete;
    }
    return HandshakeProgress::Failed;
  }

  LineResult readLine(char* output, size_t capacity, bool& truncated) {
    size_t length = 0;
    size_t discarded = 0;
    truncated = false;
    for (;;) {
      uint8_t byte = 0;
      if (!readByteWait(byte))
        return transport_.connected() ? LineResult::Timeout : LineResult::Closed;
      if (byte == '\n') {
        if (length && output[length - 1] == '\r') --length;
        output[length] = 0;
        return LineResult::Complete;
      }
      if (length + 1 < capacity) {
        output[length++] = static_cast<char>(byte);
      } else {
        truncated = true;
        if (++discarded > kMaximumDiscardedHeaderBytes)
          return LineResult::TooLong;
      }
    }
  }

  bool readByteWait(uint8_t& output) {
    const uint32_t deadline = millis() + 10000UL;
    while (static_cast<int32_t>(deadline - millis()) > 0) {
      if (!transport_.connected()) return false;
      if (takeByte(output)) return true;
      delay(1);
    }
    return false;
  }

  bool takeByte(uint8_t& output) {
    if (transport_.available() <= 0) return false;
    const int value = transport_.read();
    if (value < 0) return false;
    output = static_cast<uint8_t>(value);
    return true;
  }

  bool writeText(const char* value) {
    return value && writeBytes(reinterpret_cast<const uint8_t*>(value), strlen(value));
  }

  bool writeUnsigned(uint16_t value) {
    char digits[6] = {};
    size_t length = 0;
    do {
      digits[length++] = static_cast<char>('0' + value % 10);
      value = static_cast<uint16_t>(value / 10);
    } while (value);
    for (size_t left = 0, right = length - 1; left < right; ++left, --right) {
      const char digit = digits[left];
      digits[left] = digits[right];
      digits[right] = digit;
    }
    return writeBytes(reinterpret_cast<const uint8_t*>(digits), length);
  }

  bool writeBytes(const uint8_t* data, size_t length) {
    while (length) {
      const size_t written = transport_.write(data, length);
      if (!written) return false;
      if (sendMeasuring_) {
        ++sendWriteCalls_;
        sendWireBytes_ += written;
      }
      data += written;
      length -= written;
    }
    return true;
  }

  bool sendFrame(uint8_t opcode, const uint8_t* payload, size_t length) {
    if (state_ != State::Open || length > kMaximumMessageBytes) return false;
    if (opcode >= 0x8 && (length > kMaximumControlBytes || opcode < 0x8 || opcode > 0xA))
      return fail(Error::Protocol);
    uint8_t header[14] = {};
    uint8_t mask[4] = {};
    const uint32_t maskWord = (static_cast<uint32_t>(randomByte()) << 24) |
                              (static_cast<uint32_t>(randomByte()) << 16) |
                              (static_cast<uint32_t>(randomByte()) << 8) | randomByte();
    mask[0] = static_cast<uint8_t>(maskWord >> 24);
    mask[1] = static_cast<uint8_t>(maskWord >> 16);
    mask[2] = static_cast<uint8_t>(maskWord >> 8);
    mask[3] = static_cast<uint8_t>(maskWord);
    size_t headerLength = 2;
    header[0] = static_cast<uint8_t>(0x80 | opcode);
    header[1] = 0x80;
    if (length <= 125) {
      header[1] = static_cast<uint8_t>(header[1] | length);
    } else if (length <= 0xFFFF) {
      header[1] = 0xFE;
      header[2] = static_cast<uint8_t>(length >> 8);
      header[3] = static_cast<uint8_t>(length);
      headerLength = 4;
    } else {
      header[1] = 0xFF;
      for (uint8_t i = 0; i < 8; ++i)
        header[2 + i] = static_cast<uint8_t>(static_cast<uint64_t>(length) >> (56 - i * 8));
      headerLength = 10;
    }
    memcpy(header + headerLength, mask, sizeof(mask));
    headerLength += sizeof(mask);
    beginSendMeasurement();
    if (!writeBytes(header, headerLength)) return fail(Error::Transport);
    for (size_t offset = 0; offset < length;) {
      const size_t count = (length - offset > sizeof(scratch_)) ? sizeof(scratch_) : length - offset;
      for (size_t i = 0; i < count; ++i) scratch_[i] = payload[offset + i] ^ mask[(offset + i) & 3];
      if (!writeBytes(scratch_, count)) return fail(Error::Transport);
      offset += count;
    }
    finishSendMeasurement();
    return true;
  }

  bool sendMeasured(const uint8_t* data, size_t length) {
    beginSendMeasurement();
    if (!writeBytes(data, length)) return fail(Error::Transport);
    finishSendMeasurement();
    return true;
  }

  void beginSendMeasurement() {
    sendMeasuring_ = true;
    sendStartedMs_ = millis();
    sendWriteCalls_ = 0;
    sendWireBytes_ = 0;
  }

  void finishSendMeasurement() {
    lastSendDurationMs_ = millis() - sendStartedMs_;
    lastSendWriteCalls_ = sendWriteCalls_;
    lastSendWireBytes_ = sendWireBytes_;
    sendMeasuring_ = false;
  }

  bool readHeader() {
    while (headerBytes_ < 2) {
      if (!takeByte(header_[headerBytes_])) return false;
      ++headerBytes_;
    }
    const uint8_t indicator = header_[1] & 0x7F;
    headerNeeded_ = indicator <= 125 ? 2 : indicator == 126 ? 4 : 10;
    while (headerBytes_ < headerNeeded_) {
      if (!takeByte(header_[headerBytes_])) return false;
      ++headerBytes_;
    }
    return true;
  }

  bool startFrame() {
    const uint8_t first = header_[0];
    const uint8_t opcode = first & 0x0F;
    if ((first & 0x70) || (header_[1] & 0x80)) return fail(Error::Protocol);
    const uint8_t indicator = header_[1] & 0x7F;
    uint64_t length = indicator;
    if (indicator == 126) length = (static_cast<uint16_t>(header_[2]) << 8) | header_[3];
    if (indicator == 127) {
      length = 0;
      for (uint8_t i = 0; i < 8; ++i) {
        if (length > (UINT64_MAX >> 8)) return fail(Error::MessageTooLarge);
        length = (length << 8) | header_[2 + i];
      }
    }
    const bool control = opcode >= 0x8;
    if (control && (opcode > 0xA || (first & 0x80) == 0 ||
                    length > kMaximumControlBytes))
      return fail(Error::Protocol);
    if (!control && length > kMaximumMessageBytes - messageLength_)
      return fail(Error::MessageTooLarge);
    if (opcode == 0x2) {
      if (messageInProgress_ || messageComplete_) return fail(Error::Protocol);
      messageInProgress_ = true;
    } else if (opcode == 0x0) {
      if (!messageInProgress_) return fail(Error::Protocol);
    } else if (!control) {
      return fail(Error::Protocol);
    }
    frameOpcode_ = opcode;
    frameControl_ = control;
    frameFin_ = (first & 0x80) != 0;
    frameLength_ = length;
    frameRemaining_ = length;
    controlLength_ = 0;
    frameActive_ = true;
    headerBytes_ = 0;
    return true;
  }

  void finishFrame() {
    frameActive_ = false;
    if (!frameControl_ && frameFin_) {
      messageInProgress_ = false;
      messageComplete_ = true;
    }
  }

  bool finishControl() {
    frameActive_ = false;
    if (frameOpcode_ == 0x9) return sendFrame(0xA, control_, controlLength_);
    if (frameOpcode_ == 0x8) {
      if (!validClosePayload(control_, controlLength_)) return fail(Error::Protocol);
      if (state_ == State::Open && !sendFrame(0x8, control_, controlLength_)) return false;
      state_ = State::Closing;
      error_ = Error::PeerClosed;
      return false;
    }
    return true;
  }

  static bool validClosePayload(const uint8_t* payload, size_t length) {
    if (!payload || length == 0) return true;
    if (length < 2) return false;
    const uint16_t code = static_cast<uint16_t>(payload[0] << 8) | payload[1];
    const bool validCode = (code >= 1000 && code <= 1003) ||
                           (code >= 1007 && code <= 1011) ||
                           (code >= 3000 && code <= 4999);
    return validCode && validUtf8(payload + 2, length - 2);
  }

  static bool validUtf8(const uint8_t* input, size_t length) {
    while (length) {
      const uint8_t first = *input++;
      --length;
      if (first <= 0x7F) continue;

      uint8_t continuationCount = 0;
      uint32_t codepoint = 0;
      uint32_t minimum = 0;
      if (first >= 0xC2 && first <= 0xDF) {
        continuationCount = 1;
        codepoint = first & 0x1F;
        minimum = 0x80;
      } else if (first >= 0xE0 && first <= 0xEF) {
        continuationCount = 2;
        codepoint = first & 0x0F;
        minimum = 0x800;
      } else if (first >= 0xF0 && first <= 0xF4) {
        continuationCount = 3;
        codepoint = first & 0x07;
        minimum = 0x10000;
      } else {
        return false;
      }

      if (length < continuationCount) return false;
      for (uint8_t i = 0; i < continuationCount; ++i) {
        const uint8_t next = *input++;
        --length;
        if ((next & 0xC0) != 0x80) return false;
        codepoint = (codepoint << 6) | (next & 0x3F);
      }
      if (codepoint < minimum || codepoint > 0x10FFFF ||
          (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return false;
    }
    return true;
  }

  bool fail(Error error) {
    error_ = error;
    state_ = State::Closed;
    return false;
  }

  bool failHandshake(HandshakeFailure failure, Error error = Error::Handshake) {
    handshakeFailure_ = failure;
    return fail(error);
  }

  int failRead(Error error) {
    fail(error);
    return -1;
  }

  void reset() {
    state_ = State::Closed;
    error_ = Error::None;
    handshakeFailure_ = HandshakeFailure::None;
    handshakeStatus_ = 0;
    handshakeDeadline_ = 0;
    handshakeLineLength_ = 0;
    handshakeDiscarded_ = 0;
    handshakeHeaderCount_ = 0;
    handshakeFirstLine_ = true;
    handshakeLineTruncated_ = false;
    handshakeUpgrade_ = false;
    handshakeConnection_ = false;
    handshakeAcceptHeader_ = false;
    handshakeAccept_ = false;
    handshakeExtensions_ = false;
    memset(keyText_, 0, sizeof(keyText_));
    memset(acceptText_, 0, sizeof(acceptText_));
    memset(line_, 0, sizeof(line_));
    memset(header_, 0, sizeof(header_));
    memset(control_, 0, sizeof(control_));
    headerBytes_ = 0;
    headerNeeded_ = 2;
    frameActive_ = false;
    frameControl_ = false;
    frameFin_ = false;
    frameOpcode_ = 0;
    frameLength_ = 0;
    frameRemaining_ = 0;
    controlLength_ = 0;
    messageInProgress_ = false;
    messageComplete_ = false;
    messageLength_ = 0;
  }

  Client& transport_;
  FlovaEntropySource& entropy_;
  State state_ = State::Closed;
  Error error_ = Error::None;
  HandshakeFailure handshakeFailure_ = HandshakeFailure::None;
  uint16_t handshakeStatus_ = 0;
  uint32_t handshakeDeadline_ = 0;
  size_t handshakeLineLength_ = 0;
  size_t handshakeDiscarded_ = 0;
  uint8_t handshakeHeaderCount_ = 0;
  bool handshakeFirstLine_ = true;
  bool handshakeLineTruncated_ = false;
  bool handshakeUpgrade_ = false;
  bool handshakeConnection_ = false;
  bool handshakeAcceptHeader_ = false;
  bool handshakeAccept_ = false;
  bool handshakeExtensions_ = false;
  char keyText_[25] = {};
  char acceptText_[29] = {};
  char line_[kMaximumHeaderLineBytes] = {};
  uint8_t header_[10] = {};
  uint8_t control_[kMaximumControlBytes] = {};
  uint8_t scratch_[64] = {};
  uint8_t headerBytes_ = 0;
  uint8_t headerNeeded_ = 2;
  bool frameActive_ = false;
  bool frameControl_ = false;
  bool frameFin_ = false;
  uint8_t frameOpcode_ = 0;
  uint8_t controlLength_ = 0;
  uint64_t frameLength_ = 0;
  uint64_t frameRemaining_ = 0;
  bool messageInProgress_ = false;
  bool messageComplete_ = false;
  size_t messageLength_ = 0;
  bool sendMeasuring_ = false;
  uint32_t sendStartedMs_ = 0;
  uint32_t lastSendDurationMs_ = 0;
  uint16_t sendWriteCalls_ = 0;
  uint16_t lastSendWriteCalls_ = 0;
  size_t sendWireBytes_ = 0;
  size_t lastSendWireBytes_ = 0;
};
