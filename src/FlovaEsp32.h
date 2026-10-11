#pragma once

#include <WebServer.h>
#include <esp_system.h>

#include <FlovaEsp32BuildProfile.h>
#include <FlovaArduino.h>
#include <FlovaCustomCode.h>
#include <FlovaTlsRoots.h>
#include <FlovaEsp32Platform.h>
#include <FlovaEsp32Services.h>
#include <FlovaWifiProvisioning.h>
#include <FlovaPubSubClientAdapter.h>
#include <adapters/ArduinoFlovaLink.h>
#include <adapters/ArduinoFlovaManualHardware.h>

class FlovaEsp32Entropy : public FlovaEntropySource {
 public:
  uint8_t byte() override { return static_cast<uint8_t>(esp_random()); }
};

class FlovaEsp32 final {
 public:
  FlovaEsp32()
      : linkPlatform_(), defaultLink_(linkPlatform_, entropy_), link_(defaultLink_),
        identity_("custom_arduino_esp32"),
        client_(link_, provisioning_, network_, tlsClock_, identity_, storage_,
                clock_, logger_, entropy_, hardware_) {}

  // Inject an application-owned Flova Link transport. The default constructor
  // remains Flova Link; use the PubSubClient constructor for MQTT.
  explicit FlovaEsp32(FlovaClientLink& transport)
      : linkPlatform_(), defaultLink_(linkPlatform_, entropy_), link_(transport),
        identity_("custom_arduino_esp32"),
        client_(link_, provisioning_, network_, tlsClock_, identity_, storage_,
                clock_, logger_, entropy_, hardware_) {}

  explicit FlovaEsp32(PubSubClient& mqtt)
      : FlovaEsp32() {
    mqttSelected_ = true;
    mqttTransport_.create(&mqtt);
  }

  bool begin() { return !mqttSelected_ && client_.begin(false); }
  bool begin(const char* deviceId, const char* secret) {
    if (!mqttTransport_ || !mqttTransport_->begin(deviceId, secret) ||
        !client_.beginExternalConfiguration())
      return false;
    client_.setExternalConfigurationReportHandler(mqttConfigurationReport, this);
    mqttTransport_->onConfiguration(mqttConfiguration, this);
    return true;
  }
  void run() { run(true); }
  void run(bool networkReady) {
    if (mqttSelected_) {
      if (mqttTransport_) mqttTransport_->run(networkReady);
      client_.runExternalConfiguration();
      return;
    }
    client_.run();
  }
  bool connected() const {
    return mqttSelected_ ? (mqttTransport_ && mqttTransport_->connected()) : client_.connected();
  }
  template <typename T>
  bool report(const char* key, const T& value) { return mqttTransport_ && mqttTransport_->report(key, value); }
  template <typename T>
  flova::WriteResult flovaWrite(const char* key, const T& value) {
    if (mqttSelected_) return flova::WriteResult::failure("local_state_unavailable");
    return client_.datastream<T>(key).write(value);
  }
  template <typename T>
  bool flovaHasValue(const char* key) const {
    if (mqttSelected_) return false;
    return const_cast<FlovaEsp32*>(this)->client_.datastream<T>(key).hasValue();
  }
  template <typename T>
  T flovaValue(const char* key) const {
    if (mqttSelected_) return T();
    return const_cast<FlovaEsp32*>(this)->client_.datastream<T>(key).value();
  }
  template <typename T>
  flova::WriteResult flovaReport(const char* key, const T& value,
                                 flova::Origin origin = flova::Origin::SensorRead) {
    if (mqttSelected_)
      return mqttTransport_ && mqttTransport_->report(key, value)
                 ? flova::WriteResult::accept()
                 : flova::WriteResult::failure("mqtt_publish_failed");
    return client_.datastream<T>(key).report(value, origin);
  }
  template <typename T>
  FlovaDatastream<T, FlovaEsp32> stream(const char* key) {
    return FlovaDatastream<T, FlovaEsp32>(*this, key);
  }
  template <typename T>
  bool flovaOnWrite(const char* key, flova::ValueType valueType,
                    FlovaDatastreamBinding<T>& binding) {
    if (mqttSelected_)
      return mqttTransport_ && mqttTransport_->onDatastream(
          key, valueType, &FlovaDatastreamBinding<T>::dispatch, &binding);
    auto stream = client_.datastream<T>(key);
    switch (binding.kind) {
      case FlovaDatastreamHandlerKind::Result:
        stream.onWrite(binding.handler.result);
        break;
      case FlovaDatastreamHandlerKind::ResultWithContext:
        stream.onWrite(binding.handler.resultContext, binding.context);
        break;
      case FlovaDatastreamHandlerKind::Void:
        stream.onWrite(binding.handler.voidHandler);
        break;
      case FlovaDatastreamHandlerKind::VoidWithContext:
        stream.onWrite(binding.handler.voidContext, binding.context);
        break;
      default:
        return false;
    }
    return true;
  }
  template <typename T>
  bool flovaMode(const char* key, flova::Mode value) {
    if (mqttSelected_) return false;
    client_.datastream<T>(key).mode(value);
    return true;
  }
  template <typename T>
  bool flovaOffline(const char* key, flova::OfflinePolicy value) {
    if (mqttSelected_) return false;
    client_.datastream<T>(key).offline(value);
    return true;
  }
  template <typename T>
  bool flovaRetention(const char* key, const flova::HistoryRetentionPolicy& value) {
    if (mqttSelected_) return false;
    client_.datastream<T>(key).retention(value);
    return true;
  }
  template <typename T>
  bool flovaPersist(const char* key, flova::PersistencePolicy value) {
    if (mqttSelected_) return false;
    client_.datastream<T>(key).persist(value);
    return true;
  }
  bool heartbeat(JsonObjectConst payload) { return mqttTransport_ && mqttTransport_->heartbeat(payload); }
  bool info(JsonObjectConst payload) { return mqttTransport_ && mqttTransport_->info(payload); }
  template <typename T>
  bool acknowledgeDatastream(const char* key, const T& value, const char* commandId,
                             uint32_t desiredVersion) {
    return mqttTransport_ && mqttTransport_->acknowledgeDatastream(key, value, commandId, desiredVersion);
  }
  bool rejectDatastream(const char* key, const char* commandId, const char* error) {
    return mqttTransport_ && mqttTransport_->rejectDatastream(key, commandId, error);
  }
  bool acknowledgeConfig(JsonObjectConst payload) { return mqttTransport_ && mqttTransport_->acknowledgeConfig(payload); }
  bool subscribe(const char* key) { return mqttTransport_ && mqttTransport_->subscribe(key); }
  void onMessage(FlovaPubSubClientAdapter::MessageHandler handler, void* context = nullptr) {
    if (mqttTransport_) mqttTransport_->onMessage(handler, context);
  }
  void onMessage(FlovaPubSubClientAdapter::SimpleMessageHandler handler) {
    if (mqttTransport_) mqttTransport_->onMessage(handler);
  }
  FlovaProvisioningResponse provision(const flova::ProvisioningHandoff& input) {
    return client_.provision(input);
  }

  bool attachProvisioning(WebServer& server) {
    if (routesAttached_) return false;
    provisioningServer_ = &server;
    server.on("/status", HTTP_GET, [this]() {
      const char* error = lastError();
      snprintf(response_, sizeof(response_),
               error && error[0]
                   ? "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"can_retry\":true,\"last_error_code\":\"%s\"}"
                   : "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"can_retry\":true}",
               error ? error : "");
      provisioningServer_->send(200, "application/json", response_);
    });
    server.on("/provision", HTTP_POST, [this]() {
      const String& body = provisioningServer_->arg("plain");
      if (body.length() >= 768 ||
          !flova::parseWifiProvisioningHandoff(body.c_str(), body.length(),
                                               provisioningInput_)) {
        provisioningServer_->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_handoff\"}");
        return;
      }
      const FlovaProvisioningResponse result = provision(provisioningInput_);
      if (result == FlovaProvisioningResponse::Accepted)
        provisioningServer_->send(202, "application/json", "{\"ok\":true,\"status\":\"accepted\"}");
      else if (result == FlovaProvisioningResponse::StorageFailed)
        provisioningServer_->send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
      else
        provisioningServer_->send(409, "application/json", "{\"ok\":false,\"error\":\"invalid_state\"}");
    });
    routesAttached_ = true;
    return true;
  }

  bool startProvisioning() { return client_.startProvisioning(); }
  bool provisioning() const { return client_.provisioning(); }
  FlovaLifecycle lifecycle() const { return client_.lifecycle(); }
  bool networkConnected() const { return client_.networkConnected(); }
  bool tlsReady() const { return client_.tlsReady(); }
  bool runtimeReady() const { return client_.runtimeReady(); }
  bool ready() const { return client_.ready(); }
  FlovaPowerDownStatus prepareForPowerDown() {
    return client_.prepareForPowerDown();
  }
  const char* lastError() const { return client_.lastError(); }
  const flova::Diagnostics& diagnostics() const { return client_.diagnostics(); }
  flova::Device& device() { return client_.device(); }
  void status(FlovaStatusSnapshot& output) const { client_.status(output); }
  void setStatusListener(FlovaStatusListener listener,
                         void* context = nullptr) {
    client_.setStatusListener(listener, context);
  }
  bool setFirmwareTarget(const char* target) { return client_.setFirmwareTarget(target); }
  void enableOta(bool enabled = true) { client_.setOtaEnabled(enabled); }
  void setOtaProfile(FlovaOtaStrategy strategy, const char* bootLayoutVersion,
                     bool rollbackCapable = false) {
    client_.setOtaProfile(strategy, bootLayoutVersion, rollbackCapable);
  }
  void setRestartHandler(FlovaRestartHandler handler, void* context = nullptr) {
    client_.setRestartHandler(handler, context);
  }
  bool restartRequired() const { return client_.restartRequired(); }
  FlovaRestartReason restartReason() const { return client_.restartReason(); }
  bool factoryReset() { return client_.factoryReset(); }

  template <typename T>
  flova::Datastream<T> datastream(const char* key) {
    return client_.datastream<T>(key);
  }
  template <typename T>
  flova::Setting<T> setting(const char* key, const T& defaultValue) {
    return client_.setting<T>(key, defaultValue);
  }

 private:
  static uint8_t hexNibble(char value) {
    if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
    return 0xff;
  }

  static void mqttConfiguration(void* context, const char* phase,
                                const char* encoded, size_t length) {
    FlovaEsp32& self = *static_cast<FlovaEsp32*>(context);
    if (!phase || !encoded || length == 0 || (length & 1) != 0 ||
        length / 2 > sizeof(self.mqttConfigurationPayload_)) return;
    for (size_t i = 0; i < length; i += 2) {
      const uint8_t high = hexNibble(encoded[i]);
      const uint8_t low = hexNibble(encoded[i + 1]);
      if (high == 0xff || low == 0xff) return;
      self.mqttConfigurationPayload_[i / 2] = static_cast<uint8_t>((high << 4) | low);
    }
    FlovaLinkConfigurationRecord record = {};
    if (self.defaultLink_.decodeConfigurationPayload(
            phase, self.mqttConfigurationPayload_, length / 2, record))
      self.client_.queueExternalConfiguration(record);
  }

  static void mqttConfigurationReport(
      void* context, const FlovaLinkConfigurationReport& report) {
    FlovaEsp32& self = *static_cast<FlovaEsp32*>(context);
    if (!self.mqttTransport_) return;
    StaticJsonDocument<256> payload;
    char checksum[65] = {};
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(report.checksum); ++i) {
      checksum[i * 2] = digits[report.checksum[i] >> 4];
      checksum[i * 2 + 1] = digits[report.checksum[i] & 0x0f];
    }
    payload["generation"] = report.generation;
    payload["sequence"] = report.sequence;
    payload["status"] = report.status == FlovaLinkResultStatus::Error ? 2 : 0;
    payload["checksum"] = checksum;
    payload["reason"] = report.errorCode[0] ? report.errorCode : "ok";
    self.mqttTransport_->acknowledgeConfig(payload.as<JsonObjectConst>());
  }

  FlovaEsp32Entropy entropy_;
  FlovaEsp32Platform linkPlatform_;
  ArduinoFlovaLink defaultLink_;
  FlovaClientLink& link_;
  FlovaEsp32Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  ArduinoFlovaManualHardware hardware_;
  FlovaProvisioningAdapter provisioning_;
  FlovaEsp32ObservedNetwork network_;
  ArduinoFlovaUtcBootstrap<WiFiUDP> tlsClock_;
  FlovaEsp32Identity identity_;
  FlovaClient client_;
  bool mqttSelected_ = false;
  FlovaPhaseStorage<FlovaPubSubClientAdapter> mqttTransport_;
  WebServer* provisioningServer_ = nullptr;
  flova::ProvisioningHandoff provisioningInput_;
  char response_[192] = {};
  uint8_t mqttConfigurationPayload_[flova::link::kMaximumPayloadBytes] = {};
  bool routesAttached_ = false;
};
