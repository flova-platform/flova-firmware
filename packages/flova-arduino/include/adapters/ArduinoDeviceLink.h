#pragma once

#include <Arduino.h>

#include <FlovaLinkCbor.h>
#include <FlovaLinkCodec.h>
#include <FlovaConfigurationInstaller.h>
#include <FlovaWs.h>
#include <FlovaTlsProfile.h>
#include <FlovaTlsRoots.h>
#include <FlovaTransport.h>

extern "C" {
#include <flova_link_decode.h>
#include <flova_link_encode.h>
}

#if defined(ESP8266)
#include <user_interface.h>
#elif defined(ESP32)
#include <esp_system.h>
#endif

// The adapter is the only Arduino-facing Device Link codec. It owns a small
// deferred receive queue and one transmit frame; application code sees typed SDK
// records and never sees CBOR, JSON, or a serialized configuration document.
// Network bytes are copied into the fixed queue; board code drains it from
// loop(), where application hardware is allowed to run.
class ArduinoDeviceLink : public FlovaTransport {
 public:
#if defined(ESP8266)
  typedef BearSSL::WiFiClientSecure LinkTlsClient;
#elif defined(ESP32)
  typedef WiFiClientSecure LinkTlsClient;
#endif

  static const size_t kFrameBytes = flova::link::kMaximumFrameBytes;
  static const size_t kPayloadBytes = flova::link::kMaximumPayloadBytes;
  static const size_t kTransmitWorkspaceBytes =
      kFrameBytes + FlovaWs::kMaximumOutgoingHeaderBytes;
  // Authentication and CONFIG_BEGIN can arrive in the same network turn.
  // Two slots cover that burst without taking another TLS-sized buffer from
  // the ESP8266 heap.
  static const uint8_t kPendingFrameSlots = 2;
  static const size_t kUrlBytes = 193;
  static const uint8_t kMaximumDatastreamBindings = FLOVA_MAX_ACTIVE_DATASTREAMS;

  static_assert(kFrameBytes == 512, "Flova Link v1 frame budget changed");
  static_assert(sizeof(struct config_record) <= 384,
                "generated CONFIG_RECORD decode workspace exceeded its budget");

  ArduinoDeviceLink() : websocket_(tls_) {}
  ~ArduinoDeviceLink() override { disconnect(); }

#if defined(ESP8266)
  void setTrustAnchors(BearSSL::X509List& anchors) { trustAnchors_ = &anchors; }
#endif

  bool configure(const char* url) {
    if (!url || strlen(url) >= sizeof(url_)) return false;
    strncpy(url_, url, sizeof(url_) - 1);
    url_[sizeof(url_) - 1] = 0;
    return parseUrl();
  }

  bool setDatastreamKeys(const char* const* keys, uint8_t count) override {
    if (count > kMaximumDatastreamBindings || (count && !keys)) return false;
    for (uint8_t i = 0; i < count; ++i) {
      if (!keys[i] || !*keys[i] || strlen(keys[i]) > FLOVA_MAX_DATASTREAM_KEY_LENGTH) return false;
      for (uint8_t prior = 0; prior < i; ++prior)
        if (strcmp(keys[prior], keys[i]) == 0) return false;
      bindingKeys_[i] = keys[i];
    }
    bindingCount_ = count;
    bindingPending_ = false;
    return true;
  }

  bool decodeStoredConfigurationRecord(const uint8_t* payload, size_t length,
                                       FlovaLinkConfigurationRecord& output) {
    if (!payload || length > kPayloadBytes) return false;
    memset(&configurationDecodeWorkspace_, 0, sizeof(configurationDecodeWorkspace_));
    struct config_record& value = configurationDecodeWorkspace_;
    if (flova::link::decodeCanonical(payload, length, value, cbor_decode_config_record,
                                     cbor_encode_config_record, tx_, kPayloadBytes) != flova::link::CborResult::Complete)
      return false;
    output = FlovaLinkConfigurationRecord();
    output.phase = FlovaLinkConfigurationPhase::Record;
    output.generation = static_cast<uint32_t>(value.config_record_record_generation);
    output.sequence = static_cast<uint32_t>(value.config_record_record_sequence);
    output.recordType = static_cast<uint8_t>(value.config_record_record_body.config_record_body_choice);
    size_t encodedLength = 0;
    if (cbor_encode_config_record(output.record, FLOVA_LINK_RECORD_BYTES, &value, &encodedLength) != 0 || encodedLength > FLOVA_LINK_RECORD_BYTES)
      return false;
    output.recordLength = static_cast<uint16_t>(encodedLength);
    output.hasTypedUnit = readConfigurationUnit(output.typedUnit, value.config_record_record_body);
    if (output.recordType == 0) {
      const datastream_record& datastream = value.config_record_record_body.config_record_body_datastream_record_m;
      output.datastreamId = static_cast<DatastreamId>(datastream.datastream_record_datastream_compact_id);
      copyText(output.datastreamKey, datastream.datastream_record_datastream_key);
    }
    return output.hasTypedUnit;
  }

  bool decodeStoredConfigurationUnit(const uint8_t* payload, size_t length,
                                     flova::config::Unit& output) {
    if (!payload || length > kPayloadBytes) return false;
    memset(&configurationDecodeWorkspace_, 0, sizeof(configurationDecodeWorkspace_));
    struct config_record& value = configurationDecodeWorkspace_;
    if (flova::link::decodeCanonical(payload, length, value, cbor_decode_config_record,
                                     cbor_encode_config_record, tx_, kPayloadBytes) != flova::link::CborResult::Complete)
      return false;
    return readConfigurationUnit(output, value.config_record_record_body);
  }

  bool begin() override {
    return parseUrl();
  }

  bool connected() override {
    return active_ && websocket_.connected() &&
           (bootstrap_ || (authenticated_ && !bindingPending_));
  }

  bool connect(const char* deviceId, const char* secret) override {
    if (!parseUuid(deviceId, deviceId_) || !decodeSecret(secret, secret_) || !parseUrl()) return false;
    disconnect();
    connectionAttemptFailed_ = false;
#if defined(ESP8266)
    flova::TlsHeapStats heap;
    const flova::TlsResourceStatus resources = flova::tlsResourceStatus(flova::TlsUse::Link, &heap);
    flova::logTlsHeap("before Link", heap);
    if (!trustAnchors_ || resources != flova::TlsResourceStatus::Ready) {
      Serial.printf("[flova] Link rejected reason=%s\n",
                    trustAnchors_ ? flova::tlsResourceError(resources) : "trust_anchors_unavailable");
      return false;
    }
#endif
    if (!openConnection(false)) return false;
    const uint32_t deadline = millis() + 10000;
    while (static_cast<int32_t>(deadline - millis()) > 0) {
      loop();
      if (connected()) return true;
      if (connectionAttemptFailed_) break;
      delay(1);
    }
    disconnect();
    return false;
  }

  bool connectBootstrap(const char* token, const char* hardwareId,
                        const char* firmwareTarget, const char* secret) {
    bootstrapErrorPending_ = false;
    bootstrapError_[0] = 0;
    if (!copyToken(token, bootstrapToken_, bootstrapTokenLength_) ||
        !decodeSecret(secret, bootstrapSecret_) ||
        !copyBounded(hardwareId, bootstrapHardwareId_, sizeof(bootstrapHardwareId_)) ||
        !copyBounded(firmwareTarget, bootstrapFirmwareTarget_, sizeof(bootstrapFirmwareTarget_)) ||
        !parseUrl()) return false;
    disconnect();
    connectionAttemptFailed_ = false;
#if defined(ESP8266)
    flova::TlsHeapStats heap;
    const flova::TlsResourceStatus resources = flova::tlsResourceStatus(flova::TlsUse::Link, &heap);
    flova::logTlsHeap("before Link bootstrap", heap);
    if (!trustAnchors_ || resources != flova::TlsResourceStatus::Ready) {
      Serial.printf("[flova] Link bootstrap rejected reason=%s\n",
                    trustAnchors_ ? flova::tlsResourceError(resources) : "trust_anchors_unavailable");
      return false;
    }
#endif
    return openConnection(true);
  }

  void setConfigurationGeneration(uint32_t generation) override {
    configurationGeneration_ = generation;
  }

  bool publishState(const FlovaLinkStateBatch& message) override {
    if (!connected() || !message.count || message.count > FLOVA_LINK_MAX_STATE_READINGS) return false;
    struct state value = {};
    value.state_generation = message.configurationGeneration;
    value.state_values.state_readings_state_reading_m_count = message.count;
    for (uint8_t i = 0; i < message.count; ++i) {
      struct state_reading& reading = value.state_values.state_readings_state_reading_m[i];
      reading.state_reading_reading_compact_id = message.readings[i].datastreamId;
      reading.state_reading_reading_revision = message.readings[i].revision;
      if (!fillTypedValue(reading.state_reading_typed_value_fields_m, message.readings[i].value)) return false;
    }
    return sendEncoded(0x11, message.messageId, value, cbor_encode_state);
  }

  bool publishCommandResult(const FlovaLinkCommandResult& message) override {
    if (!connected()) return false;
    struct command_result_r value = {};
    const bool ok = message.status == FlovaLinkResultStatus::Ok ||
                    message.status == FlovaLinkResultStatus::Duplicate;
    if (ok) {
      value.command_result_choice = command_result_r::command_result_ok_m_c;
      struct command_result_ok& result = value.command_result_ok_m;
      result.command_result_ok_result_ok_generation = configurationGeneration_;
      if (!setId(result.command_result_ok_result_ok_command_id, message.commandId)) return false;
      result.command_result_ok_result_ok_compact_id = message.datastreamId;
      result.command_result_ok_result_ok_status = message.duplicate ? 2 : 0;
      result.command_result_ok_result_ok_version = message.desiredVersion;
      if (!fillTypedValue(result.command_result_ok_typed_value_fields_m, message.value) ||
          !fillCorrelation(result.command_result_ok_result_ok_correlation_id, message.correlationId)) return false;
    } else {
      value.command_result_choice = command_result_r::command_result_error_m_c;
      struct command_result_error& result = value.command_result_error_m;
      result.command_result_error_result_error_generation = configurationGeneration_;
      if (!setId(result.command_result_error_result_error_command_id, message.commandId) ||
          !setText(result.command_result_error_result_error_code, message.errorCode, sizeof(message.errorCode)) ||
          !setText(result.command_result_error_result_error_message, message.errorCode, sizeof(message.errorCode)) ||
          !fillCorrelation(result.command_result_error_result_error_correlation_id, message.correlationId)) return false;
      result.command_result_error_result_error_compact_id = message.datastreamId;
      result.command_result_error_result_error_status = 3;
      result.command_result_error_result_error_version = message.desiredVersion;
    }
    return sendEncoded(0x12, message.messageId, value, cbor_encode_command_result);
  }

  bool publishHeartbeat(const FlovaLinkHeartbeat& message) override {
    if (!connected()) return false;
    struct heartbeat value = {};
    value.heartbeat_generation = configurationGeneration_;
    value.heartbeat_uptime_ms = message.uptimeMs;
    value.heartbeat_status = message.otaCapable ? 1 : 0;
    if (!setText(value.heartbeat_firmware_version, message.firmwareVersion, sizeof(message.firmwareVersion))) return false;
    return sendEncoded(0x10, message.messageId, value, cbor_encode_heartbeat);
  }

  bool publishConfigurationReport(const FlovaLinkConfigurationReport& message) override {
    if (!connected()) return false;
    struct config_ack value = {};
    value.config_ack_ack_generation = message.generation;
    value.config_ack_ack_sequence = message.sequence;
    value.config_ack_ack_status = message.status == FlovaLinkResultStatus::Error
                                      ? 2
                                      : message.status == FlovaLinkResultStatus::Duplicate ? 1 : 0;
    const char* reason = message.errorCode[0] ? message.errorCode : "ok";
    if (!setText(value.config_ack_ack_reason, reason, sizeof(message.errorCode))) return false;
    return sendEncoded(0x18, message.messageId, value, cbor_encode_config_ack);
  }

  bool publishConfigurationState(const FlovaLinkConfigurationState& message) override {
    if (!connected()) return false;
    struct config_reported value = {};
    value.config_reported_reported_config_generation = message.generation;
    value.config_reported_reported_config_status =
        message.status == FlovaLinkResultStatus::Error ? 2 : 0;
    value.config_reported_reported_config_checksum.value = message.checksum;
    value.config_reported_reported_config_checksum.len = sizeof(message.checksum);
    return sendEncoded(0x13, message.messageId, value, cbor_encode_config_reported);
  }

  bool publishOtaReport(const FlovaLinkOtaReport& message) override {
    if (!connected()) return false;
    struct ota_reported value = {};
    if (!setId(value.ota_reported_reported_ota_id, message.installId) ||
        !setText(value.ota_reported_reported_ota_error, message.errorCode, sizeof(message.errorCode))) return false;
    value.ota_reported_reported_ota_status = static_cast<uint8_t>(message.status);
    return sendEncoded(0x14, message.messageId, value, cbor_encode_ota_reported);
  }

  bool publishScheduleStatus(const FlovaLinkScheduleStatus& message) override {
    if (!connected()) return false;
    struct schedule_reported value = {};
    value.schedule_reported_reported_schedule_generation = message.generation;
    value.schedule_reported_reported_schedule_revision = message.revision;
    value.schedule_reported_reported_schedule_status = static_cast<uint8_t>(message.status);
    static const uint8_t emptyChecksum[32] = {};
    value.schedule_reported_reported_schedule_checksum.value = emptyChecksum;
    value.schedule_reported_reported_schedule_checksum.len = 32;
    return sendEncoded(0x15, message.messageId, value, cbor_encode_schedule_reported);
  }

  bool publishScheduleRenew(const FlovaLinkScheduleStatus& message) override {
    if (!connected()) return false;
    struct schedule_renew value = {};
    value.schedule_renew_renew_generation = message.generation;
    value.schedule_renew_renew_revision = message.revision;
    return sendEncoded(0x16, message.messageId, value, cbor_encode_schedule_renew);
  }

  bool publishTimeRequest(const FlovaLinkTimeRequest& message) override {
    if (!connected()) return false;
    struct time_request value = {};
    value.time_request_id = message.messageId;
    value.time_request_monotonic_ms = message.monotonicMs;
    return sendEncoded(0x17, message.messageId, value, cbor_encode_time_request);
  }

  void setCallback(FlovaMessageCallback callback) override { callback_ = callback; }
  void loop() override {
    if (!active_) return;
#if defined(ESP8266)
    pumpTransmit();
    for (uint8_t i = 0; i < kPendingFrameSlots && active_; ++i) {
      pumpWebSocket();
      if (!txLength_) dispatchPendingFrame();
      pumpTransmit();
    }
#else
    pumpWebSocket();
    dispatchPendingFrame();
#endif
  }

  void dispatchPendingFrame() {
    // Decode and dispatch only from the board-owned loop. Keep a few frames
    // because the Engine may answer authentication and queue the first
    // configuration frame in the same network turn.
    if (!pendingFrameCount_ || !active_) return;
    const uint8_t slot = pendingFrameHead_;
    const size_t frameLength = pendingFrameLengths_[slot];
    pendingFrameHead_ = static_cast<uint8_t>((pendingFrameHead_ + 1) % kPendingFrameSlots);
    --pendingFrameCount_;
    flova::link::FrameView frame = {};
    if (flova::link::decodeWebSocketBinaryMessage(pendingFrames_[slot], frameLength, frame) !=
        flova::link::FrameResult::Complete) {
      Serial.println("[flova] Link frame rejected=invalid_header");
      disconnect();
      return;
    }
#if FLOVA_LINK_PERFORMANCE_LOGGING
    Serial.printf("[flova] Link frame received type=0x%02x id=%llu bytes=%u queue_ms=%lu\n",
                  static_cast<unsigned>(frame.messageType),
                  static_cast<unsigned long long>(frame.messageId),
                  static_cast<unsigned>(frameLength),
                  static_cast<unsigned long>(millis() - pendingFrameQueuedAtMs_[slot]));
#endif
    handleFrame(frame);
    // Configuration decoding uses generated CBOR structs that are too large
    // for the ESP8266 callback stack. Dispatch only after handleFrame() has
    // returned and those temporary structs have been released.
    if (pendingCallback_ && callback_ && active_) {
      pendingCallback_ = false;
      callback_(inbound_);
    }
  }
  bool takeBootstrapError() {
    const bool pending = bootstrapErrorPending_;
    bootstrapErrorPending_ = false;
    return pending;
  }
  bool takeBootstrapError(char* output, size_t capacity) {
    const bool pending = bootstrapErrorPending_;
    if (pending && output && capacity) {
      const size_t length = strnlen(bootstrapError_, capacity - 1);
      memcpy(output, bootstrapError_, length);
      output[length] = 0;
    }
    bootstrapErrorPending_ = false;
    return pending;
  }
  void disconnect() override {
    if (disconnecting_) return;
    disconnecting_ = true;
    const bool hadConnection = active_ || authenticated_ || bootstrap_ ||
                               pendingFrameCount_ != 0 || tls_.connected();
    active_ = false;
    authenticated_ = false;
    bindingPending_ = false;
    bootstrap_ = false;
    pendingFrameCount_ = 0;
    pendingFrameHead_ = 0;
    pendingFrameTail_ = 0;
    pendingFrameLength_ = 0;
#if defined(ESP8266)
    txLength_ = 0;
    txOffset_ = 0;
#endif
    pendingCallback_ = false;
    websocket_.close();
    tls_.stop();
#if defined(ESP8266)
    if (hadConnection) flova::logTlsHeap("after Link disconnect");
#endif
    disconnecting_ = false;
  }

 private:
  typedef int (*Encoder)(uint8_t*, size_t, const void*, size_t*);

  bool openConnection(bool bootstrap) {
#if defined(ESP8266)
    flova::configureLinkTls(tls_, *trustAnchors_, time(nullptr));
    {
      // BearSSL allocates its TLS control blocks during connect. Keep that
      // allocation in the configured IRAM heap rather than fragmented DRAM.
      HeapSelectIram tlsHeap;
      if (!tls_.connect(host_, port_)) {
        flova::logLinkTlsFailure(tls_);
        tls_.stop();
        connectionAttemptFailed_ = true;
        return false;
      }
    }
#else
    flova::configureLinkTls(tls_);
    if (!tls_.connect(host_, port_)) {
      Serial.printf("[flova] Link TLS connect failed host=%s port=%u\n",
                    host_, static_cast<unsigned>(port_));
      tls_.stop();
      connectionAttemptFailed_ = true;
      return false;
    }
#endif
    // The ESP8266 wrapper forwards this to its real private TCP context via
    // the guarded framework patch used by Link builds.
    tls_.setNoDelay(true);
    if (!websocket_.handshake(host_, port_, path_)) {
      Serial.printf("[flova] Link websocket handshake failed code=%u reason=%s status=%u\n",
                    static_cast<unsigned>(websocket_.error()),
                    FlovaWs::handshakeFailureName(websocket_.handshakeFailure()),
                    static_cast<unsigned>(websocket_.handshakeStatus()));
      tls_.stop();
      connectionAttemptFailed_ = true;
      return false;
    }
    active_ = true;
    bootstrap_ = bootstrap;
    authenticated_ = false;
#if defined(ESP8266)
    flova::logTlsHeap("after Link WSS");
#endif
    if (bootstrap_ ? !sendBootstrapAuthentication() : !sendAuthentication()) {
      connectionAttemptFailed_ = true;
      disconnect();
      return false;
    }
    return true;
  }

  void pumpWebSocket() {
    if (pendingFrameCount_ >= kPendingFrameSlots) {
      connectionAttemptFailed_ = true;
      disconnect();
      return;
    }
    const uint8_t slot = pendingFrameTail_;
    const size_t capacity = kFrameBytes - pendingFrameLength_;
    const int length = websocket_.read(pendingFrames_[slot] + pendingFrameLength_, capacity);
    if (length < 0) {
      connectionAttemptFailed_ = true;
      Serial.printf("[flova] Link websocket error code=%u\n",
                    static_cast<unsigned>(websocket_.error()));
      disconnect();
      return;
    }
    pendingFrameLength_ += static_cast<size_t>(length);
    if (websocket_.messageComplete()) {
      if (!pendingFrameLength_ || pendingFrameLength_ > kFrameBytes) {
        connectionAttemptFailed_ = true;
        disconnect();
        return;
      }
      pendingFrameLengths_[slot] = pendingFrameLength_;
#if FLOVA_LINK_PERFORMANCE_LOGGING
      pendingFrameQueuedAtMs_[slot] = millis();
#endif
      pendingFrameTail_ = static_cast<uint8_t>((pendingFrameTail_ + 1) % kPendingFrameSlots);
      ++pendingFrameCount_;
      pendingFrameLength_ = 0;
    }
  }

#if defined(ESP8266)
  void pumpTransmit() {
    if (!active_) return;
    if (!tls_.pollNonBlocking()) {
      connectionAttemptFailed_ = true;
      disconnect();
      return;
    }
    if (!txLength_) return;
    const int accepted = tls_.writeNonBlocking(tx_ + txOffset_, txLength_ - txOffset_);
    if (accepted < 0) {
      connectionAttemptFailed_ = true;
      disconnect();
      return;
    }
    txOffset_ += static_cast<size_t>(accepted);
    if (txOffset_ != txLength_) return;
#if FLOVA_LINK_PERFORMANCE_LOGGING
    Serial.printf("[flova] Link TX drained bytes=%u queue_ms=%lu\n",
                  static_cast<unsigned>(txLength_),
                  static_cast<unsigned long>(millis() - txQueuedAtMs_));
#endif
    txLength_ = 0;
    txOffset_ = 0;
  }
#endif

  template <typename T>
  bool sendEncoded(uint8_t type, uint64_t messageId, const T& value, int (*encoder)(uint8_t*, size_t, const T*, size_t*)) {
#if FLOVA_LINK_PERFORMANCE_LOGGING
    const uint32_t startedAt = millis();
#endif
    size_t payloadLength = 0;
    uint8_t* frame = tx_ + FlovaWs::kMaximumOutgoingHeaderBytes;
#if defined(ESP8266)
    if (txLength_) return false;
#endif
    if (flova::link::encodeCanonical(frame + flova::link::kHeaderBytes, kPayloadBytes, value, encoder, payloadLength) != flova::link::CborResult::Complete ||
        !flova::link::encodeFrameHeader(frame, kFrameBytes, type, 0, messageId, payloadLength)) return false;
#if FLOVA_LINK_PERFORMANCE_LOGGING
    const uint32_t encodedAt = millis();
#endif
#if defined(ESP8266)
    size_t wireLength = 0;
    if (!websocket_.prepareBinary(frame, flova::link::kHeaderBytes + payloadLength,
                                  tx_, sizeof(tx_), wireLength))
      return false;
    txLength_ = wireLength;
    txOffset_ = 0;
#if FLOVA_LINK_PERFORMANCE_LOGGING
    txQueuedAtMs_ = millis();
#endif
    pumpTransmit();
    const bool sent = active_;
#else
    const bool sent = websocket_.sendBinaryCoalesced(
        frame, flova::link::kHeaderBytes + payloadLength, tx_, sizeof(tx_));
#endif
#if FLOVA_LINK_PERFORMANCE_LOGGING
    Serial.printf("[flova] Link send type=0x%02x id=%llu bytes=%u encode_ms=%lu send_ms=%lu writes=%u wire_bytes=%u accepted=%u\n",
                  static_cast<unsigned>(type),
                  static_cast<unsigned long long>(messageId),
                  static_cast<unsigned>(flova::link::kHeaderBytes + payloadLength),
                  static_cast<unsigned long>(encodedAt - startedAt),
                  static_cast<unsigned long>(websocket_.lastSendDurationMs()),
                  static_cast<unsigned>(websocket_.lastSendWriteCalls()),
                  static_cast<unsigned>(websocket_.lastSendWireBytes()), sent ? 1U : 0U);
#endif
    return sent;
  }

  bool sendAuthentication() {
    struct auth value = {};
    value.auth_device_id.value = deviceId_;
    value.auth_device_id.len = sizeof(deviceId_);
    value.auth_secret.value = secret_;
    value.auth_secret.len = sizeof(secret_);
    return sendEncoded(0x01, 0, value, cbor_encode_auth);
  }

  bool sendDatastreamBinding() {
    struct datastream_bind value = {};
    value.datastream_bind_binding_generation = configurationGeneration_;
    value.datastream_bind_binding_keys.datastream_binding_keys_tstr1_48_count = bindingCount_;
    for (uint8_t i = 0; i < bindingCount_; ++i) {
      const size_t length = strlen(bindingKeys_[i]);
      if (!length || length > FLOVA_MAX_DATASTREAM_KEY_LENGTH) return false;
      value.datastream_bind_binding_keys.datastream_binding_keys_tstr1_48[i].value =
          reinterpret_cast<const uint8_t*>(bindingKeys_[i]);
      value.datastream_bind_binding_keys.datastream_binding_keys_tstr1_48[i].len = length;
    }
    return sendEncoded(0x09, 0, value, cbor_encode_datastream_bind);
  }

  bool sendBootstrapAuthentication() {
    struct bootstrap_auth value = {};
    value.bootstrap_auth_bootstrap_token.value = bootstrapToken_;
    value.bootstrap_auth_bootstrap_token.len = bootstrapTokenLength_;
    value.bootstrap_auth_bootstrap_secret.value = bootstrapSecret_;
    value.bootstrap_auth_bootstrap_secret.len = sizeof(bootstrapSecret_);
    if (!setText(value.bootstrap_auth_hardware_id, bootstrapHardwareId_, sizeof(bootstrapHardwareId_)) ||
        !setText(value.bootstrap_auth_firmware_target, bootstrapFirmwareTarget_, sizeof(bootstrapFirmwareTarget_)))
      return false;
    value.bootstrap_auth_bootstrap_capabilities.capabilities_datastream_slots = FLOVA_DATASTREAM_CAPACITY;
    value.bootstrap_auth_bootstrap_capabilities.capabilities_input_slots = FLOVA_HARDWARE_INPUT_CAPACITY;
    value.bootstrap_auth_bootstrap_capabilities.capabilities_output_slots = FLOVA_HARDWARE_OUTPUT_CAPACITY;
    value.bootstrap_auth_bootstrap_capabilities.capabilities_command_slots = FLOVA_COMMAND_DEDUP_CAPACITY;
    value.bootstrap_auth_bootstrap_capabilities.capabilities_schedule_slots = FLOVA_SCHEDULE_RUNTIME_ENABLED ? FLOVA_SCHEDULE_CAPACITY : 0;
    value.bootstrap_auth_bootstrap_capabilities.capabilities_manifest_bytes = 0;
    value.bootstrap_auth_bootstrap_capabilities.capabilities_history_bytes = 0;
    return sendEncoded(0x06, 0, value, cbor_encode_bootstrap_auth);
  }

  void handleFrame(const flova::link::FrameView& frame) {
    if (frame.messageType == 0x02) {
      struct auth_ok value = {};
      if (decode(frame, value, cbor_decode_auth_ok, cbor_encode_auth_ok)) {
        authenticated_ = true;
        bindingPending_ = bindingCount_ != 0;
        if (bindingPending_ && !sendDatastreamBinding()) disconnect();
#if defined(ESP8266)
        flova::logTlsHeap("after Link auth");
#endif
      } else {
        Serial.println("[flova] Link auth response rejected=auth_ok_decode_failed");
        disconnect();
      }
      return;
    }
    if (frame.messageType == 0x0a && authenticated_) return handleDatastreamBound(frame);
    if (frame.messageType == 0x03) {
      zcbor_string reason = {};
      if (decode(frame, reason, cbor_decode_auth_error, cbor_encode_auth_error)) {
        Serial.printf("[flova] Link auth rejected reason=%.*s\n",
                      static_cast<int>(reason.len),
                      reinterpret_cast<const char*>(reason.value));
      } else {
        Serial.println("[flova] Link auth rejected reason=auth_error_decode_failed");
      }
      disconnect();
      return;
    }
    if (frame.messageType == 0x07 && bootstrap_) {
      struct bootstrap_committed value = {};
      if (!decode(frame, value, cbor_decode_bootstrap_committed,
                  cbor_encode_bootstrap_committed)) return disconnect();
      inbound_ = FlovaLinkInboundMessage();
      inbound_.type = FlovaLinkMessageType::BootstrapCommitted;
      inbound_.messageId = frame.messageId;
      copyId(inbound_.body.bootstrapCommitted.deviceId, value.bootstrap_committed_committed_device_id);
      inbound_.body.bootstrapCommitted.generation = static_cast<uint32_t>(value.bootstrap_committed_committed_generation);
      inbound_.body.bootstrapCommitted.serverUtcMs = value.bootstrap_committed_committed_server_utc_ms;
      pendingCallback_ = true;
      return;
    }
    if (frame.messageType == 0x08 && bootstrap_) {
      zcbor_string reason = {};
      if (decode(frame, reason, cbor_decode_bootstrap_error,
                 cbor_encode_bootstrap_error)) {
        copyText(bootstrapError_, reason);
        bootstrapErrorPending_ = true;
        Serial.printf("[flova] bootstrap error=%.*s\n",
                      static_cast<int>(reason.len),
                      reinterpret_cast<const char*>(reason.value));
      }
      disconnect();
      return;
    }
    if (!authenticated_ && !bootstrap_) return;
    if (frame.messageType == 0x20) return handleCommand(frame);
    if (frame.messageType == 0x21) {
      // INGESTION_ACK has an empty payload; the frame id identifies the
      // state message being acknowledged.
      inbound_ = FlovaLinkInboundMessage();
      inbound_.type = FlovaLinkMessageType::Acknowledgement;
      inbound_.messageId = frame.messageId;
      inbound_.body.acknowledgement.acknowledgedMessageId = frame.messageId;
      pendingCallback_ = true;
      return;
    }
    if (frame.messageType == 0x26) {
      struct flow_control value = {};
      if (!decode(frame, value, cbor_decode_flow_control, cbor_encode_flow_control)) return disconnect();
      inbound_ = FlovaLinkInboundMessage();
      inbound_.type = FlovaLinkMessageType::FlowControl;
      inbound_.messageId = frame.messageId;
      inbound_.body.acknowledgement.acknowledgedMessageId = frame.messageId;
      inbound_.body.acknowledgement.retryAfterMs = static_cast<uint32_t>(value.flow_control_flow_retry_after_ms);
      copyText(inbound_.body.acknowledgement.reasonCode, value.flow_control_flow_reason);
      pendingCallback_ = true;
      return;
    }
    if (frame.messageType == 0x27) {
      zcbor_string reason = {};
      if (!decode(frame, reason, cbor_decode_message_rejected, cbor_encode_message_rejected)) return disconnect();
      inbound_ = FlovaLinkInboundMessage();
      inbound_.type = FlovaLinkMessageType::Rejection;
      inbound_.messageId = frame.messageId;
      inbound_.body.acknowledgement.acknowledgedMessageId = frame.messageId;
      copyText(inbound_.body.acknowledgement.reasonCode, reason);
      pendingCallback_ = true;
      return;
    }
    if (frame.messageType == 0x23) return handleOta(frame);
    if (frame.messageType == 0x25) return handleTime(frame);
    if (frame.messageType == 0x28 || frame.messageType == 0x29 || frame.messageType == 0x2a)
      return handleConfiguration(frame);
  }

  template <typename T>
  bool decode(const flova::link::FrameView& frame, T& value,
              int (*decoder)(const uint8_t*, size_t, T*, size_t*),
              int (*encoder)(uint8_t*, size_t, const T*, size_t*)) {
    return flova::link::decodeCanonical(frame.payload, frame.payloadLength, value, decoder,
                                        encoder,
                                        tx_, kPayloadBytes) == flova::link::CborResult::Complete;
  }

  void handleCommand(const flova::link::FrameView& frame) {
    struct command value = {};
    if (!decode(frame, value, cbor_decode_command, cbor_encode_command) ||
        value.command_id.len != sizeof(deviceId_)) return disconnect();
    inbound_ = FlovaLinkInboundMessage();
    inbound_.type = FlovaLinkMessageType::Command;
    inbound_.messageId = frame.messageId;
    inbound_.body.command.configurationGeneration = static_cast<uint32_t>(value.command_generation);
    inbound_.body.command.datastreamId = static_cast<DatastreamId>(value.command_compact_id);
    inbound_.body.command.desiredVersion = static_cast<uint32_t>(value.command_desired_version);
    inbound_.body.command.expiresAtUtcMs = value.command_expires_at_utc_ms;
    copyId(inbound_.body.command.commandId, value.command_id);
    if (!readCorrelation(inbound_.body.command.correlationId, value.command_correlation_id) ||
        !readTypedValue(inbound_.body.command.value, value.command_typed_value_fields_m)) return disconnect();
    pendingCallback_ = true;
  }

  void handleDatastreamBound(const flova::link::FrameView& frame) {
    struct datastream_bound value = {};
    if (!decode(frame, value, cbor_decode_datastream_bound, cbor_encode_datastream_bound) ||
        value.datastream_bound_bound_ids.datastream_bound_ids_compact_id_m_count != bindingCount_ ||
        value.datastream_bound_bound_generation > UINT32_MAX)
      return disconnect();
    inbound_ = FlovaLinkInboundMessage();
    inbound_.type = FlovaLinkMessageType::DatastreamBound;
    inbound_.messageId = frame.messageId;
    inbound_.body.datastreamBound.generation = static_cast<uint32_t>(value.datastream_bound_bound_generation);
    inbound_.body.datastreamBound.count = static_cast<uint8_t>(
        value.datastream_bound_bound_ids.datastream_bound_ids_compact_id_m_count);
    for (uint8_t i = 0; i < inbound_.body.datastreamBound.count; ++i) {
      const DatastreamId id = static_cast<DatastreamId>(
          value.datastream_bound_bound_ids.datastream_bound_ids_compact_id_m[i]);
      if (!flovaValidDatastreamId(id)) return disconnect();
      for (uint8_t prior = 0; prior < i; ++prior)
        if (inbound_.body.datastreamBound.ids[prior] == id) return disconnect();
      inbound_.body.datastreamBound.ids[i] = id;
    }
    bindingPending_ = false;
    pendingCallback_ = true;
  }

  void handleOta(const flova::link::FrameView& frame) {
    struct ota_desired value = {};
    if (!decode(frame, value, cbor_decode_ota_desired, cbor_encode_ota_desired)) return disconnect();
    inbound_ = FlovaLinkInboundMessage();
    inbound_.type = FlovaLinkMessageType::OtaOffer;
    inbound_.messageId = frame.messageId;
    copyId(inbound_.body.otaOffer.installId, value.ota_desired_ota_install_id);
    copyText(inbound_.body.otaOffer.version, value.ota_desired_ota_version);
    copyText(inbound_.body.otaOffer.url, value.ota_desired_ota_url);
    if (!copyHex(inbound_.body.otaOffer.sha256, sizeof(inbound_.body.otaOffer.sha256),
                 value.ota_desired_ota_checksum)) return disconnect();
    inbound_.body.otaOffer.sizeBytes = static_cast<uint32_t>(value.ota_desired_ota_size);
    pendingCallback_ = true;
  }

  void handleTime(const flova::link::FrameView& frame) {
    struct time_response value = {};
    if (!decode(frame, value, cbor_decode_time_response, cbor_encode_time_response)) return disconnect();
    inbound_ = FlovaLinkInboundMessage();
    inbound_.type = FlovaLinkMessageType::TimeResponse;
    inbound_.messageId = frame.messageId;
    inbound_.body.timeResponse.requestId = value.time_response_request_id;
    inbound_.body.timeResponse.serverUtcMs = value.time_response_server_utc_ms;
    pendingCallback_ = true;
  }

  void handleConfiguration(const flova::link::FrameView& frame) {
    inbound_ = FlovaLinkInboundMessage();
    inbound_.type = FlovaLinkMessageType::ConfigurationRecord;
    inbound_.messageId = frame.messageId;
    inbound_.body.configuration.messageId = frame.messageId;
    inbound_.body.configuration.phase = frame.messageType == 0x28 ? FlovaLinkConfigurationPhase::Begin :
                                       frame.messageType == 0x29 ? FlovaLinkConfigurationPhase::Record :
                                                                  FlovaLinkConfigurationPhase::End;
    size_t encodedLength = 0;
    if (frame.messageType == 0x28) {
      struct config_begin value = {};
      if (!decode(frame, value, cbor_decode_config_begin, cbor_encode_config_begin)) return disconnect();
      inbound_.body.configuration.generation = static_cast<uint32_t>(value.config_begin_config_generation);
      inbound_.body.configuration.recordCount = static_cast<uint32_t>(value.config_begin_config_record_count);
      inbound_.body.configuration.schemaVersion = 1;
      inbound_.body.configuration.maximumRecordBytes = flova::config::kMaximumRecordBytes;
      copyBytes(inbound_.body.configuration.checksum, value.config_begin_config_checksum);
      inbound_.body.configuration.recordLength = 0;
    } else if (frame.messageType == 0x29) {
      memset(&configurationDecodeWorkspace_, 0, sizeof(configurationDecodeWorkspace_));
      struct config_record& value = configurationDecodeWorkspace_;
      if (!decode(frame, value, cbor_decode_config_record, cbor_encode_config_record) ||
          cbor_encode_config_record(inbound_.body.configuration.record, FLOVA_LINK_RECORD_BYTES, &value, &encodedLength) != 0 ||
          encodedLength > FLOVA_LINK_RECORD_BYTES) return disconnect();
      inbound_.body.configuration.generation = static_cast<uint32_t>(value.config_record_record_generation);
      inbound_.body.configuration.sequence = static_cast<uint32_t>(value.config_record_record_sequence);
      inbound_.body.configuration.recordType = static_cast<uint8_t>(value.config_record_record_body.config_record_body_choice);
      if (value.config_record_record_body.config_record_body_choice ==
          config_record_body_r::config_record_body_datastream_record_m_c) {
        const struct datastream_record& datastream =
            value.config_record_record_body.config_record_body_datastream_record_m;
        inbound_.body.configuration.datastreamId =
            static_cast<DatastreamId>(datastream.datastream_record_datastream_compact_id);
        copyText(inbound_.body.configuration.datastreamKey,
                 datastream.datastream_record_datastream_key);
      }
      inbound_.body.configuration.recordLength = static_cast<uint16_t>(encodedLength);
      inbound_.body.configuration.hasTypedUnit = readConfigurationUnit(
          inbound_.body.configuration.typedUnit, value.config_record_record_body);
      if (!inbound_.body.configuration.hasTypedUnit) return disconnect();
#if defined(ESP8266)
      flova::logTlsHeap("during Link configuration record");
#endif
    } else {
      struct config_end value = {};
      if (!decode(frame, value, cbor_decode_config_end, cbor_encode_config_end)) return disconnect();
      inbound_.body.configuration.generation = static_cast<uint32_t>(value.config_end_end_generation);
      inbound_.body.configuration.recordCount = static_cast<uint32_t>(value.config_end_end_record_count);
      inbound_.body.configuration.schemaVersion = 1;
      inbound_.body.configuration.maximumRecordBytes = flova::config::kMaximumRecordBytes;
      copyBytes(inbound_.body.configuration.checksum, value.config_end_end_checksum);
    }
    pendingCallback_ = true;
  }

  static bool setText(struct zcbor_string& out, const char* value, size_t maxLength) {
    if (!value) return false;
    const size_t length = strnlen(value, maxLength);
    if (length >= maxLength) return false;
    out.value = reinterpret_cast<const uint8_t*>(value);
    out.len = length;
    return true;
  }

  static bool setId(struct zcbor_string& out, const FlovaLinkId& id) {
    if (!id.present) return false;
    out.value = id.bytes;
    out.len = sizeof(id.bytes);
    return true;
  }

  static void copyId(FlovaLinkId& out, const struct zcbor_string& input) {
    out = FlovaLinkId();
    if (input.len == sizeof(out.bytes)) {
      memcpy(out.bytes, input.value, sizeof(out.bytes));
      out.present = true;
    }
  }

  static void copyBytes(uint8_t (&out)[32], const struct zcbor_string& input) {
    memset(out, 0, sizeof(out));
    if (input.len == sizeof(out)) memcpy(out, input.value, sizeof(out));
  }

  static void copyText(char* out, const struct zcbor_string& input) {
    if (!out) return;
    const size_t maxLength = FLOVA_TEXT_CAPACITY;
    const size_t length = input.len < maxLength - 1 ? input.len : maxLength - 1;
    memcpy(out, input.value, length);
    out[length] = 0;
  }

  static bool copyUuidText(char (&out)[37], const struct zcbor_string& input) {
    static const char hex[] = "0123456789abcdef";
    if (input.len != 16) return false;
    size_t cursor = 0;
    for (size_t i = 0; i < 16; ++i) {
      if (i == 4 || i == 6 || i == 8 || i == 10) out[cursor++] = '-';
      out[cursor++] = hex[input.value[i] >> 4];
      out[cursor++] = hex[input.value[i] & 0x0f];
    }
    out[cursor] = 0;
    return true;
  }

  static bool readConfigurationValue(flova::config::Value& out,
                                     const struct typed_value_fields_r& input) {
    switch (input.typed_value_fields_choice) {
      case typed_value_fields_r::typed_value_fields_value_bool_type_l_c:
        out.kind = flova::config::ValueKind::Boolean;
        out.data.boolean = input.typed_value_fields_value_bool_type_l_value_bool;
        return true;
      case typed_value_fields_r::typed_value_fields_value_int_type_l_c:
        out.kind = flova::config::ValueKind::Int64;
        out.data.integer = input.typed_value_fields_value_int_type_l_value_int;
        return true;
      case typed_value_fields_r::typed_value_fields_value_f32_type_l_c:
        out.kind = flova::config::ValueKind::Float32;
        out.data.float32 = input.typed_value_fields_value_f32_type_l_value_f32;
        return true;
      case typed_value_fields_r::typed_value_fields_value_f64_type_l_c:
        out.kind = flova::config::ValueKind::Float64;
        out.data.float64 = input.typed_value_fields_value_f64_type_l_value_f64;
        return true;
      case typed_value_fields_r::typed_value_fields_value_text_type_l_c:
        if (input.typed_value_fields_value_text_type_l_value_text.len >= FLOVA_TEXT_CAPACITY) return false;
        out.kind = flova::config::ValueKind::Text;
        memcpy(out.data.text, input.typed_value_fields_value_text_type_l_value_text.value,
               input.typed_value_fields_value_text_type_l_value_text.len);
        out.data.text[input.typed_value_fields_value_text_type_l_value_text.len] = 0;
        return true;
    }
    return false;
  }

  static bool readConfigurationUnit(flova::config::Unit& out,
                                     const struct config_record_body_r& input) {
    out = flova::config::Unit();
    switch (input.config_record_body_choice) {
      case config_record_body_r::config_record_body_datastream_record_m_c: {
        const datastream_record& source = input.config_record_body_datastream_record_m;
        out.kind = flova::config::UnitKind::Datastream;
        if (source.datastream_record_datastream_compact_id == 0 ||
            source.datastream_record_datastream_compact_id > UINT16_MAX ||
            source.datastream_record_datastream_value_type > 4 ||
            !copyUuidText(out.data.datastream.uuid, source.datastream_record_datastream_uuid) ||
            source.datastream_record_datastream_key.len >= FLOVA_TEXT_CAPACITY) return false;
        out.data.datastream.id = static_cast<DatastreamId>(source.datastream_record_datastream_compact_id);
        out.data.datastream.valueType = static_cast<uint8_t>(source.datastream_record_datastream_value_type);
        copyText(out.data.datastream.key, source.datastream_record_datastream_key);
        if (source.datastream_record_datastream_minimum_present) {
          if (!readConfigurationValue(out.data.datastream.minimum, source.datastream_record_datastream_minimum.datastream_record_datastream_minimum)) return false;
          out.data.datastream.hasMinimum = true;
        }
        if (source.datastream_record_datastream_maximum_present) {
          if (!readConfigurationValue(out.data.datastream.maximum, source.datastream_record_datastream_maximum.datastream_record_datastream_maximum)) return false;
          out.data.datastream.hasMaximum = true;
        }
        if (source.datastream_record_datastream_default_present) {
          if (!readConfigurationValue(out.data.datastream.defaultValue, source.datastream_record_datastream_default.datastream_record_datastream_default)) return false;
          out.data.datastream.hasDefault = true;
        }
        if (source.datastream_record_datastream_mapping_present) {
          const hardware_mapping& mapping = source.datastream_record_datastream_mapping.datastream_record_datastream_mapping;
          if (mapping.hardware_mapping_mapping_kind > 3 || mapping.hardware_mapping_mapping_pin > UINT16_MAX) return false;
          out.data.datastream.hasMapping = true;
          out.data.datastream.mapping.kind = static_cast<flova::config::MappingKind>(mapping.hardware_mapping_mapping_kind);
          out.data.datastream.mapping.pin = static_cast<uint16_t>(mapping.hardware_mapping_mapping_pin);
          out.data.datastream.mapping.hasActiveHigh = mapping.hardware_mapping_mapping_active_high_present;
          out.data.datastream.mapping.activeHigh = mapping.hardware_mapping_mapping_active_high.hardware_mapping_mapping_active_high;
          out.data.datastream.mapping.hasPull = mapping.hardware_mapping_mapping_pull_present;
          out.data.datastream.mapping.pull = static_cast<uint8_t>(mapping.hardware_mapping_mapping_pull.hardware_mapping_mapping_pull);
          out.data.datastream.mapping.hasDebounceMs = mapping.hardware_mapping_mapping_debounce_ms_present;
          out.data.datastream.mapping.debounceMs = static_cast<uint32_t>(mapping.hardware_mapping_mapping_debounce_ms.hardware_mapping_mapping_debounce_ms);
          out.data.datastream.mapping.hasSampleMs = mapping.hardware_mapping_mapping_sample_ms_present;
          out.data.datastream.mapping.sampleMs = static_cast<uint32_t>(mapping.hardware_mapping_mapping_sample_ms.hardware_mapping_mapping_sample_ms);
          out.data.datastream.mapping.hasMinimumOutputMs = mapping.hardware_mapping_mapping_min_output_ms_present;
          out.data.datastream.mapping.minimumOutputMs = static_cast<uint32_t>(mapping.hardware_mapping_mapping_min_output_ms.hardware_mapping_mapping_min_output_ms);
        }
        return true;
      }
      case config_record_body_r::config_record_body_system_record_m_c: {
        const system_record& source = input.config_record_body_system_record_m;
        out.kind = flova::config::UnitKind::System;
        out.data.system.hasHeartbeatMs = source.system_record_system_heartbeat_ms_present;
        out.data.system.heartbeatMs = static_cast<uint32_t>(source.system_record_system_heartbeat_ms.system_record_system_heartbeat_ms);
        out.data.system.hasStatusLedPin = source.system_record_system_status_led_pin_present;
        out.data.system.statusLedPin = static_cast<uint8_t>(source.system_record_system_status_led_pin.system_record_system_status_led_pin);
        out.data.system.hasStatusLedActiveLow = source.system_record_system_status_led_active_low_present;
        out.data.system.statusLedActiveLow = source.system_record_system_status_led_active_low.system_record_system_status_led_active_low;
        out.data.system.hasBatchFlushMs = source.system_record_system_batch_flush_ms_present;
        out.data.system.batchFlushMs = static_cast<uint32_t>(source.system_record_system_batch_flush_ms.system_record_system_batch_flush_ms);
        return true;
      }
      case config_record_body_r::config_record_body_schedule_record_m_c: {
        const schedule_record& source = input.config_record_body_schedule_record_m;
        if (source.schedule_record_schedule_actions_schedule_action_m_count > 8 || source.schedule_record_schedule_id > UINT32_MAX) return false;
        out.kind = flova::config::UnitKind::Schedule;
        out.data.schedule.id = static_cast<uint32_t>(source.schedule_record_schedule_id);
        out.data.schedule.enabled = source.schedule_record_schedule_enabled;
        out.data.schedule.validFrom = source.schedule_record_schedule_valid_from;
        out.data.schedule.validUntil = source.schedule_record_schedule_valid_until;
        out.data.schedule.actionCount = static_cast<uint8_t>(source.schedule_record_schedule_actions_schedule_action_m_count);
        for (uint8_t i = 0; i < out.data.schedule.actionCount; ++i) {
          const schedule_action& action = source.schedule_record_schedule_actions_schedule_action_m[i];
          if (action.schedule_action_action_offset_ms > UINT32_MAX || action.schedule_action_action_compact_id == 0 || action.schedule_action_action_compact_id > UINT16_MAX ||
              !readConfigurationValue(out.data.schedule.actions[i].value, action.schedule_action_typed_value_fields_m)) return false;
          out.data.schedule.actions[i].offsetMs = static_cast<uint32_t>(action.schedule_action_action_offset_ms);
          out.data.schedule.actions[i].datastreamId = static_cast<DatastreamId>(action.schedule_action_action_compact_id);
        }
        return true;
      }
      case config_record_body_r::config_record_body_schedule_occurrence_record_m_c: {
        const schedule_occurrence_record& source = input.config_record_body_schedule_occurrence_record_m;
        if (source.schedule_occurrence_record_occurrence_schedule_id > UINT32_MAX ||
            source.schedule_occurrence_record_occurrence_chunk_index > UINT16_MAX ||
            source.schedule_occurrence_record_occurrence_chunk_count == 0 ||
            source.schedule_occurrence_record_occurrence_chunk_count > UINT16_MAX ||
            source.schedule_occurrence_record_occurrence_values_uint64_m_count == 0 ||
            source.schedule_occurrence_record_occurrence_values_uint64_m_count > flova::config::kConfigurationOccurrenceChunk) return false;
        out.kind = flova::config::UnitKind::ScheduleOccurrences;
        out.data.occurrences.scheduleId = static_cast<uint32_t>(source.schedule_occurrence_record_occurrence_schedule_id);
        out.data.occurrences.chunkIndex = static_cast<uint16_t>(source.schedule_occurrence_record_occurrence_chunk_index);
        out.data.occurrences.chunkCount = static_cast<uint16_t>(source.schedule_occurrence_record_occurrence_chunk_count);
        out.data.occurrences.occurrenceCount = static_cast<uint8_t>(source.schedule_occurrence_record_occurrence_values_uint64_m_count);
        for (uint8_t i = 0; i < out.data.occurrences.occurrenceCount; ++i) {
          out.data.occurrences.occurrences[i] = source.schedule_occurrence_record_occurrence_values_uint64_m[i];
        }
        return true;
      }
      case config_record_body_r::config_record_body_safety_record_m_c: {
        const safety_record& source = input.config_record_body_safety_record_m;
        if (source.safety_record_safety_compact_id == 0 || source.safety_record_safety_compact_id > UINT16_MAX || source.safety_record_safety_policy > 4) return false;
        out.kind = flova::config::UnitKind::Safety;
        out.data.safety.datastreamId = static_cast<DatastreamId>(source.safety_record_safety_compact_id);
        out.data.safety.policy = static_cast<flova::config::SafetyPolicy>(source.safety_record_safety_policy);
        if (source.safety_record_safety_minimum_present) {
          if (!readConfigurationValue(out.data.safety.minimum, source.safety_record_safety_minimum.safety_record_safety_minimum)) return false;
          out.data.safety.hasMinimum = true;
        }
        if (source.safety_record_safety_maximum_present) {
          if (!readConfigurationValue(out.data.safety.maximum, source.safety_record_safety_maximum.safety_record_safety_maximum)) return false;
          out.data.safety.hasMaximum = true;
        }
        out.data.safety.hasTimeoutMs = source.safety_record_safety_timeout_ms_present;
        out.data.safety.timeoutMs = static_cast<uint32_t>(source.safety_record_safety_timeout_ms.safety_record_safety_timeout_ms);
        return true;
      }
    }
    return false;
  }

  static bool copyHex(char* out, size_t outSize, const struct zcbor_string& input) {
    static const char hex[] = "0123456789abcdef";
    if (!out || input.len != 32 || outSize < 65) return false;
    for (size_t i = 0; i < input.len; ++i) {
      out[i * 2] = hex[input.value[i] >> 4];
      out[i * 2 + 1] = hex[input.value[i] & 0x0f];
    }
    out[64] = 0;
    return true;
  }

  static bool fillCorrelation(struct correlation_id_r& out, const FlovaLinkId& id) {
    out.correlation_id_choice = id.present ? correlation_id_r::correlation_id_uuid_m_c : correlation_id_r::correlation_id_empty_id_m_c;
    out.correlation_id_uuid_m.value = id.bytes;
    out.correlation_id_uuid_m.len = id.present ? sizeof(id.bytes) : 0;
    return true;
  }

  static bool readCorrelation(FlovaLinkId& out, const struct correlation_id_r& input) {
    out = FlovaLinkId();
    if (input.correlation_id_choice == correlation_id_r::correlation_id_empty_id_m_c) return true;
    if (input.correlation_id_uuid_m.len != sizeof(out.bytes)) return false;
    memcpy(out.bytes, input.correlation_id_uuid_m.value, sizeof(out.bytes));
    out.present = true;
    return true;
  }

  static bool fillTypedValue(struct typed_value_fields_r& out, const FlovaLinkValue& value) {
    switch (value.kind) {
      case FlovaLinkValueKind::Bool:
        out.typed_value_fields_choice = typed_value_fields_r::typed_value_fields_value_bool_type_l_c;
        out.typed_value_fields_value_bool_type_l_value_bool = value.data.boolean;
        return true;
      case FlovaLinkValueKind::Int64:
        out.typed_value_fields_choice = typed_value_fields_r::typed_value_fields_value_int_type_l_c;
        out.typed_value_fields_value_int_type_l_value_int = value.data.integer;
        return true;
      case FlovaLinkValueKind::Float32:
        out.typed_value_fields_choice = typed_value_fields_r::typed_value_fields_value_f32_type_l_c;
        out.typed_value_fields_value_f32_type_l_value_f32 = value.data.float32;
        return true;
      case FlovaLinkValueKind::Float64:
        out.typed_value_fields_choice = typed_value_fields_r::typed_value_fields_value_f64_type_l_c;
        out.typed_value_fields_value_f64_type_l_value_f64 = value.data.float64;
        return true;
      case FlovaLinkValueKind::Text:
        out.typed_value_fields_choice = typed_value_fields_r::typed_value_fields_value_text_type_l_c;
        out.typed_value_fields_value_text_type_l_value_text.value = reinterpret_cast<const uint8_t*>(value.data.text);
        out.typed_value_fields_value_text_type_l_value_text.len = strnlen(value.data.text, sizeof(value.data.text));
        return out.typed_value_fields_value_text_type_l_value_text.len < sizeof(value.data.text);
    }
    return false;
  }

  static bool readTypedValue(FlovaLinkValue& out, const struct typed_value_fields_r& value) {
    out = FlovaLinkValue();
    switch (value.typed_value_fields_choice) {
      case typed_value_fields_r::typed_value_fields_value_bool_type_l_c:
        out.kind = FlovaLinkValueKind::Bool;
        out.data.boolean = value.typed_value_fields_value_bool_type_l_value_bool;
        return true;
      case typed_value_fields_r::typed_value_fields_value_int_type_l_c:
        out.kind = FlovaLinkValueKind::Int64;
        out.data.integer = value.typed_value_fields_value_int_type_l_value_int;
        return true;
      case typed_value_fields_r::typed_value_fields_value_f32_type_l_c:
        out.kind = FlovaLinkValueKind::Float32;
        out.data.float32 = value.typed_value_fields_value_f32_type_l_value_f32;
        return true;
      case typed_value_fields_r::typed_value_fields_value_f64_type_l_c:
        out.kind = FlovaLinkValueKind::Float64;
        out.data.float64 = value.typed_value_fields_value_f64_type_l_value_f64;
        return true;
      case typed_value_fields_r::typed_value_fields_value_text_type_l_c:
        if (value.typed_value_fields_value_text_type_l_value_text.len >= sizeof(out.data.text)) return false;
        out.kind = FlovaLinkValueKind::Text;
        memcpy(out.data.text, value.typed_value_fields_value_text_type_l_value_text.value,
               value.typed_value_fields_value_text_type_l_value_text.len);
        out.data.text[value.typed_value_fields_value_text_type_l_value_text.len] = 0;
        return true;
    }
    return false;
  }

  static bool parseUuid(const char* text, uint8_t (&out)[16]) {
    if (!text || strlen(text) != 36) return false;
    uint8_t index = 0;
    bool high = true;
    uint8_t nibble = 0;
    for (uint8_t i = 0; i < 36; ++i) {
      if (text[i] == '-') continue;
      const char c = text[i];
      const int value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (value < 0 || index >= 16) return false;
      if (high) nibble = static_cast<uint8_t>(value << 4);
      else out[index++] = static_cast<uint8_t>(nibble | value);
      high = !high;
    }
    return index == 16 && high;
  }

  static bool decodeSecret(const char* text, uint8_t (&out)[32]) {
    if (!text) return false;
    const size_t length = strlen(text);
    uint32_t bits = 0;
    uint8_t available = 0;
    uint8_t output = 0;
    for (size_t i = 0; i < length; ++i) {
      const char c = text[i];
      const int value = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '-' ? 62 : c == '_' ? 63 : -1;
      if (value < 0) return false;
      bits = (bits << 6) | static_cast<uint32_t>(value);
      available = static_cast<uint8_t>(available + 6);
      while (available >= 8) {
        available = static_cast<uint8_t>(available - 8);
        if (output >= sizeof(out)) return false;
        out[output++] = static_cast<uint8_t>(bits >> available);
      }
    }
    return output == sizeof(out);
  }

  static bool copyToken(const char* text, uint8_t (&out)[64], size_t& length) {
    if (!text) return false;
    length = strlen(text);
    if (length < 32 || length > sizeof(out)) return false;
    memcpy(out, text, length);
    return true;
  }

  static bool copyBounded(const char* input, char* output, size_t capacity) {
    if (!input || !output || !capacity) return false;
    const size_t length = strnlen(input, capacity);
    if (length >= capacity) return false;
    memcpy(output, input, length + 1);
    return true;
  }

  bool parseUrl() {
    if (strncmp(url_, "wss://", 6) != 0) return false;
    const char* authority = url_ + 6;
    const char* slash = strchr(authority, '/');
    const size_t authorityLength = slash ? static_cast<size_t>(slash - authority) : strlen(authority);
    if (!authorityLength || authorityLength >= sizeof(host_)) return false;
    const char* colon = static_cast<const char*>(memchr(authority, ':', authorityLength));
    const size_t hostLength = colon ? static_cast<size_t>(colon - authority) : authorityLength;
    if (!hostLength || hostLength >= sizeof(host_)) return false;
    for (size_t i = 0; i < hostLength; ++i) {
      const uint8_t character = static_cast<uint8_t>(authority[i]);
      if (character < 0x21 || character == 0x7F || authority[i] == '/' ||
          authority[i] == '?' || authority[i] == '#' || authority[i] == '@')
        return false;
    }
    if (colon && static_cast<size_t>(colon - authority) + 1 >= authorityLength) return false;
    if (colon && memchr(colon + 1, ':', authorityLength - hostLength - 1)) return false;
    memcpy(host_, authority, hostLength);
    host_[hostLength] = 0;
    if (colon) {
      uint32_t parsedPort = 0;
      for (const char* cursor = colon + 1; cursor < authority + authorityLength; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
        parsedPort = parsedPort * 10 + static_cast<uint32_t>(*cursor - '0');
        if (parsedPort > 65535) return false;
      }
      if (!parsedPort) return false;
      port_ = static_cast<uint16_t>(parsedPort);
    } else {
      port_ = 443;
    }
    if (slash) {
      if (strlen(slash) >= sizeof(path_)) return false;
      strcpy(path_, slash);
    } else {
      strcpy(path_, "/");
    }
    return true;
  }

  char url_[kUrlBytes] = {};
  char host_[kUrlBytes] = {};
  char path_[kUrlBytes] = {};
  uint16_t port_ = 443;
  bool active_ = false;
  bool authenticated_ = false;
  bool bootstrap_ = false;
  LinkTlsClient tls_;
  FlovaWs websocket_;
  uint8_t deviceId_[16] = {};
  uint8_t secret_[32] = {};
  uint8_t bootstrapToken_[64] = {};
  size_t bootstrapTokenLength_ = 0;
  uint8_t bootstrapSecret_[32] = {};
  char bootstrapError_[FLOVA_TEXT_CAPACITY] = {};
  char bootstrapHardwareId_[97] = {};
  char bootstrapFirmwareTarget_[65] = {};
  uint8_t tx_[kTransmitWorkspaceBytes] = {};
#if defined(ESP8266)
  size_t txLength_ = 0;
  size_t txOffset_ = 0;
#if FLOVA_LINK_PERFORMANCE_LOGGING
  uint32_t txQueuedAtMs_ = 0;
#endif
#endif
  const char* bindingKeys_[kMaximumDatastreamBindings] = {};
  uint8_t bindingCount_ = 0;
  bool bindingPending_ = false;
  uint32_t configurationGeneration_ = 0;
  FlovaMessageCallback callback_ = nullptr;
  bool disconnecting_ = false;
  bool connectionAttemptFailed_ = false;
  bool bootstrapErrorPending_ = false;
  uint8_t pendingFrames_[kPendingFrameSlots][kFrameBytes] = {};
  size_t pendingFrameLengths_[kPendingFrameSlots] = {};
#if FLOVA_LINK_PERFORMANCE_LOGGING
  uint32_t pendingFrameQueuedAtMs_[kPendingFrameSlots] = {};
#endif
  uint8_t pendingFrameHead_ = 0;
  uint8_t pendingFrameTail_ = 0;
  uint8_t pendingFrameCount_ = 0;
  size_t pendingFrameLength_ = 0;
  bool pendingCallback_ = false;
  FlovaLinkInboundMessage inbound_ = {};
  struct config_record configurationDecodeWorkspace_ = {};
#if defined(ESP8266)
  BearSSL::X509List* trustAnchors_ = nullptr;
#endif
};
