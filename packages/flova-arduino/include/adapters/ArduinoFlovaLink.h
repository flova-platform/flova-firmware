#pragma once

#include <Arduino.h>
#include <new>

#include <FlovaCore.h>
#include <FlovaClientLink.h>
#include <FlovaTlsRoots.h>
#include "ArduinoDeviceLink.h"
#include "ArduinoOtaInstaller.h"

// Canonical flova::Link adapter for Arduino projects. It deliberately maps
// only the core state/command/time contract; provisioning, OTA, schedules,
// and configuration installation remain opt-in board services.
class ArduinoFlovaLink : public FlovaClientLink {
 public:
  explicit ArduinoFlovaLink(FlovaEntropySource& entropy)
      : transport_(entropy), messageNonce_(readNonce(entropy)) {}

  ~ArduinoFlovaLink() override {
#if defined(ESP8266)
    delete trustAnchors_;
#endif
  }

  bool configure(const char* url, const char* deviceId, const char* secret) {
    if (!copy(url, url_, sizeof(url_)) || !copy(deviceId, deviceIdText_, sizeof(deviceIdText_)) ||
        !copy(secret, secretText_, sizeof(secretText_))) return false;
    return transport_.configure(url_);
  }

  bool beginBootstrap(const char* url, const char* token, const char* hardwareId,
                      const char* firmwareTarget, const char* secret) {
    if (!copy(url, url_, sizeof(url_)) || !transport_.configure(url_)) return false;
    if (!ensureTransport()) return false;
    return transport_.connectBootstrap(token, hardwareId, firmwareTarget, secret);
  }

  void pollBootstrap() { transport_.loop(); }

  bool takeBootstrapCommitted(FlovaLinkBootstrapCommitted& output) {
    if (!bootstrapCommittedPending_) return false;
    output = bootstrapCommitted_;
    bootstrapCommittedPending_ = false;
    return true;
  }

  bool takeBootstrapError(char* output, size_t capacity) {
    return transport_.takeBootstrapError(output, capacity);
  }

  bool takeConfigurationRecord(FlovaLinkConfigurationRecord& output) override {
    if (!configurationPending_) return false;
    output = configuration_;
    configurationPending_ = false;
    return true;
  }

  bool publishConfigurationReport(
      const FlovaLinkConfigurationReport& report) override {
    return transport_.publishConfigurationReport(report);
  }

  bool publishConfigurationState(
      const FlovaLinkConfigurationState& state) override {
    return transport_.publishConfigurationState(state);
  }

  bool publishHeartbeat(const FlovaLinkHeartbeat& heartbeat) override {
    return transport_.publishHeartbeat(heartbeat);
  }

  bool publishScheduleStatus(const FlovaLinkScheduleStatus& status) override {
    return transport_.publishScheduleStatus(status);
  }

  bool publishScheduleRenew(const FlovaLinkScheduleStatus& status) override {
    return transport_.publishScheduleRenew(status);
  }

  bool takeOtaOffer(FlovaLinkOtaOffer& offer) override {
    if (!otaPending_) return false;
    offer = otaOffer_;
    otaPending_ = false;
    return true;
  }

  bool publishOtaReport(const FlovaLinkOtaReport& report) override {
    return transport_.publishOtaReport(report);
  }

  flova::OtaInstallResult installOta(
      const FlovaLinkOtaOffer& input) override {
#if defined(ESP8266)
    if (!trustAnchors_) return flova::OtaInstallResult::ResourceUnavailable;
    otaInstaller_.setTrustAnchors(*trustAnchors_);
#endif
    return otaInstaller_.install(input);
  }

  bool decodeStoredConfigurationRecord(
      const uint8_t* payload, size_t length,
      FlovaLinkConfigurationRecord& output) override {
    return transport_.decodeStoredConfigurationRecord(payload, length, output);
  }

  void setConfigurationGeneration(uint32_t generation) override {
    configurationGeneration_ = generation;
    transport_.setConfigurationGeneration(generation);
  }

  uint32_t configurationGeneration() const override {
    return configurationGeneration_;
  }

  bool resourceRecoveryRequired() const override {
    return resourceUnavailable_ || transport_.resourceRecoveryRequired();
  }

  void disconnect() { transport_.disconnect(); }

#if defined(ESP8266)
  void setTrustAnchors(BearSSL::X509List& anchors) { transport_.setTrustAnchors(anchors); }
#endif

  bool begin() override {
    if (!ensureTransport()) return false;
    // Socket/TLS setup starts here; WebSocket authentication and binding
    // continue cooperatively from poll(). Application work remains in run().
    const bool connectedNow = transport_.connect(deviceIdText_, secretText_);
    nextReconnectAt_ = millis() + (connectedNow ? 5000UL : 1000UL);
    // A temporary Wi-Fi/TLS failure must not make application setup
    // irreversible. run() retries through the same bounded transport path.
    return true;
  }

  bool connected() const override {
    return const_cast<ArduinoDeviceLink&>(transport_).connected() && bound_;
  }

  bool send(const flova::Message& message) override {
    if (!connected()) return false;
    if (message.kind == flova::MessageKind::StateUpdate) {
      FlovaLinkStateBatch batch = {};
      batch.messageId = message.messageId;
      batch.configurationGeneration = configurationGeneration_;
      batch.count = 1;
      batch.readings[0].datastreamId = message.datastreamId;
      batch.readings[0].revision = message.revision;
      if (!toLinkValue(batch.readings[0].value, message.value)) return false;
      return transport_.publishState(batch);
    }
    if (message.kind == flova::MessageKind::TimeRequest) {
      FlovaLinkTimeRequest request = {};
      request.messageId = message.messageId;
      request.monotonicMs = message.monotonic;
      pendingTimeRequestId_ = request.messageId;
      flova::Value::copy(pendingTimeCommandId_, message.commandId);
      return transport_.publishTimeRequest(request);
    }
    if (message.kind != flova::MessageKind::Acknowledgement && message.kind != flova::MessageKind::Error)
      return false;

    FlovaLinkCommandResult result = {};
    result.messageId = message.messageId;
    if (!parseId(message.commandId, result.commandId)) return false;
    if (!parseOptionalId(message.correlationId, result.correlationId)) return false;
    result.desiredVersion = message.revision;
    result.datastreamId = message.datastreamId;
    result.status = message.kind == flova::MessageKind::Acknowledgement
                        ? FlovaLinkResultStatus::Ok
                        : FlovaLinkResultStatus::Error;
    result.duplicate = false;
    if (!toLinkValue(result.value, message.value)) return false;
    strncpy(result.errorCode, message.reason, sizeof(result.errorCode) - 1);
    result.errorCode[sizeof(result.errorCode) - 1] = 0;
    return transport_.publishCommandResult(result);
  }

  void poll() override {
    transport_.loop();
    if (!transport_.connected() && static_cast<int32_t>(millis() - nextReconnectAt_) >= 0) {
      bound_ = false;
      transport_.connect(deviceIdText_, secretText_);
      nextReconnectAt_ = millis() + 5000UL;
    }
  }

  void setReceiver(flova::MessageReceiver receiver, void* context) override {
    receiver_ = receiver;
    receiverContext_ = context;
  }

  uint32_t messageNonce() const override { return messageNonce_; }

  bool bindDatastreams(const char* const* keys, size_t count, DatastreamId* ids) override {
    if (!keys || !ids || !count || count > ArduinoDeviceLink::kMaximumDatastreamBindings) return false;
    if (count > 255) return false;
    bindingCount_ = static_cast<uint8_t>(count);
    for (size_t i = 0; i < count; ++i) {
      bindingKeys_[i] = keys[i];
      ids[i] = FLOVA_INVALID_DATASTREAM_ID;
    }
    bound_ = false;
    return transport_.setDatastreamKeys(keys, bindingCount_);
  }

  bool bindingReady() const override { return bindingCount_ == 0 || bound_; }

  bool readDatastreamBinding(DatastreamId* ids, size_t count) const override {
    if (count != bindingCount_ || (count && !ids) || !bindingReady()) return false;
    for (size_t i = 0; i < count; ++i) ids[i] = boundIds_[i];
    return true;
  }

 private:
  static uint32_t readNonce(FlovaEntropySource& entropy) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < 4; ++i)
      value = (value << 8) | entropy.byte();
    return value ? value : 1;
  }

  static bool copy(const char* source, char* target, size_t capacity) {
    if (!source || !target || !capacity) return false;
    const size_t length = strlen(source);
    if (!length || length >= capacity) return false;
    memcpy(target, source, length + 1);
    return true;
  }

  bool ensureTransport() {
    resourceUnavailable_ = false;
    if (!configured_ && !transport_.configure(url_)) return false;
#if defined(ESP8266)
    if (!trustAnchors_) {
      trustAnchors_ = new (std::nothrow) BearSSL::X509List(FLOVA_TLS_ROOT_CERTS);
      if (!trustAnchors_) {
        resourceUnavailable_ = true;
        return false;
      }
      transport_.setTrustAnchors(*trustAnchors_);
    }
#endif
    transport_.setConfigurationGeneration(configurationGeneration_);
    transport_.setCallbackContext(receive, this);
    configured_ = transport_.begin();
    return configured_;
  }

  static int hex(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  }

  static bool parseId(const char* text, FlovaLinkId& output) {
    output = FlovaLinkId();
    if (!text || strlen(text) != 32) return false;
    for (size_t i = 0; i < sizeof(output.bytes); ++i) {
      const int high = hex(text[i * 2]);
      const int low = hex(text[i * 2 + 1]);
      if (high < 0 || low < 0) return false;
      output.bytes[i] = static_cast<uint8_t>((high << 4) | low);
    }
    output.present = true;
    return true;
  }

  static bool parseOptionalId(const char* text, FlovaLinkId& output) {
    output = FlovaLinkId();
    return !text || !text[0] || parseId(text, output);
  }

  static bool toLinkValue(FlovaLinkValue& output, const flova::Value& value) {
    output = FlovaLinkValue();
    switch (value.type) {
      case flova::ValueType::Boolean: output.kind = FlovaLinkValueKind::Bool; output.data.boolean = value.scalar.boolean; return true;
      case flova::ValueType::Int64: output.kind = FlovaLinkValueKind::Int64; output.data.integer = value.scalar.integer; return true;
      case flova::ValueType::Float: output.kind = FlovaLinkValueKind::Float32; output.data.float32 = value.scalar.floating; return true;
      case flova::ValueType::Double: output.kind = FlovaLinkValueKind::Float64; output.data.float64 = value.scalar.number; return true;
      case flova::ValueType::Text:
        output.kind = FlovaLinkValueKind::Text;
        strncpy(output.data.text, value.text, sizeof(output.data.text) - 1);
        output.data.text[sizeof(output.data.text) - 1] = 0;
        return true;
    }
    return false;
  }

  static bool fromLinkValue(flova::Value& output, const FlovaLinkValue& value) {
    switch (value.kind) {
      case FlovaLinkValueKind::Bool: output = flova::Value::from(value.data.boolean); return true;
      case FlovaLinkValueKind::Int64: output = flova::Value::from(value.data.integer); return true;
      case FlovaLinkValueKind::Float32: output = flova::Value::from(value.data.float32); return true;
      case FlovaLinkValueKind::Float64: output = flova::Value::from(value.data.float64); return true;
      case FlovaLinkValueKind::Text: output = flova::Value::from(value.data.text); return true;
    }
    return false;
  }

  static flova::Origin origin(bool userCommand) {
    return userCommand ? flova::Origin::UserCommand : flova::Origin::CloudAutomation;
  }

  static void idText(char* output, size_t capacity, const FlovaLinkId& id) {
    static const char digits[] = "0123456789abcdef";
    if (!output || capacity < 33 || !id.present) { if (output && capacity) output[0] = 0; return; }
    for (size_t i = 0; i < sizeof(id.bytes); ++i) {
      output[i * 2] = digits[id.bytes[i] >> 4];
      output[i * 2 + 1] = digits[id.bytes[i] & 0x0f];
    }
    output[32] = 0;
  }

  static void receive(void* context, const FlovaLinkInboundMessage& inbound) {
    if (!context) return;
    static_cast<ArduinoFlovaLink*>(context)->accept(inbound);
  }

  void accept(const FlovaLinkInboundMessage& inbound) {
    if (inbound.type == FlovaLinkMessageType::BootstrapCommitted) {
      bootstrapCommitted_ = inbound.body.bootstrapCommitted;
      bootstrapCommittedPending_ = true;
      return;
    }
    if (inbound.type == FlovaLinkMessageType::OtaOffer) {
      if (otaPending_) return;
      otaOffer_ = inbound.body.otaOffer;
      otaPending_ = true;
      return;
    }
    if (inbound.type == FlovaLinkMessageType::ConfigurationBegin ||
        inbound.type == FlovaLinkMessageType::ConfigurationRecord ||
        inbound.type == FlovaLinkMessageType::ConfigurationEnd) {
      if (configurationPending_) return;
      configuration_ = inbound.body.configuration;
      configurationPending_ = true;
      return;
    }
    if (inbound.type == FlovaLinkMessageType::DatastreamBound) {
      if (inbound.body.datastreamBound.count != bindingCount_) return;
      for (uint8_t i = 0; i < bindingCount_; ++i) boundIds_[i] = inbound.body.datastreamBound.ids[i];
      configurationGeneration_ = inbound.body.datastreamBound.generation;
      transport_.setConfigurationGeneration(configurationGeneration_);
      bound_ = true;
      return;
    }
    if (!receiver_) return;
    flova::Message message;
    if (inbound.type == FlovaLinkMessageType::Acknowledgement) {
      message.kind = flova::MessageKind::Acknowledgement;
      message.messageId = inbound.body.acknowledgement.acknowledgedMessageId;
    } else if (inbound.type == FlovaLinkMessageType::FlowControl) {
      message.kind = flova::MessageKind::FlowControl;
      message.messageId = inbound.body.acknowledgement.acknowledgedMessageId;
      message.retryAfterMs = inbound.body.acknowledgement.retryAfterMs;
    } else if (inbound.type == FlovaLinkMessageType::Rejection) {
      message.kind = flova::MessageKind::Rejection;
      message.messageId = inbound.body.acknowledgement.acknowledgedMessageId;
      strncpy(message.reason, inbound.body.acknowledgement.reasonCode,
              sizeof(message.reason) - 1);
    } else if (inbound.type == FlovaLinkMessageType::Command) {
      message.kind = flova::MessageKind::WriteRequest;
      message.datastreamId = inbound.body.command.datastreamId;
      message.revision = inbound.body.command.desiredVersion;
      message.expiresAtUtcMs = inbound.body.command.expiresAtUtcMs;
      message.origin = origin(inbound.body.command.isUserCommand);
      idText(message.commandId, sizeof(message.commandId), inbound.body.command.commandId);
      idText(message.correlationId, sizeof(message.correlationId), inbound.body.command.correlationId);
      if (!fromLinkValue(message.value, inbound.body.command.value)) return;
    } else if (inbound.type == FlovaLinkMessageType::TimeResponse) {
      if (inbound.body.timeResponse.requestId != pendingTimeRequestId_) return;
      message.kind = flova::MessageKind::TimeResponse;
      message.timestamp = inbound.body.timeResponse.serverUtcMs;
      flova::Value::copy(message.commandId, pendingTimeCommandId_);
      pendingTimeRequestId_ = 0;
    } else {
      return;
    }
    receiver_(receiverContext_, message);
  }

  ArduinoDeviceLink transport_;
  const char* bindingKeys_[ArduinoDeviceLink::kMaximumDatastreamBindings] = {};
  DatastreamId boundIds_[ArduinoDeviceLink::kMaximumDatastreamBindings] = {};
  uint8_t bindingCount_ = 0;
  bool bound_ = false;
  bool configured_ = false;
  bool bootstrapCommittedPending_ = false;
  bool configurationPending_ = false;
  bool otaPending_ = false;
  bool resourceUnavailable_ = false;
  uint32_t nextReconnectAt_ = 0;
  uint32_t configurationGeneration_ = 0;
  uint32_t messageNonce_;
  uint64_t pendingTimeRequestId_ = 0;
  char pendingTimeCommandId_[flova::kMaxText] = {};
  char url_[193] = {};
  char deviceIdText_[64] = {};
  char secretText_[96] = {};
  flova::MessageReceiver receiver_ = nullptr;
  void* receiverContext_ = nullptr;
  FlovaLinkBootstrapCommitted bootstrapCommitted_ = {};
  FlovaLinkConfigurationRecord configuration_ = {};
  FlovaLinkOtaOffer otaOffer_ = {};
  ArduinoOtaInstaller otaInstaller_;
#if defined(ESP8266)
  BearSSL::X509List* trustAnchors_ = nullptr;
#endif
};
