#pragma once

#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>
#include <FlovaArduino.h>
#include <FlovaConfiguration.h>
#include <FlovaLinkCodec.h>
#include <FlovaLinkConfigurationStorage.h>
#include <FlovaProvisioningHandoff.h>
#include <FlovaTlsProfile.h>

#ifndef FLOVA_FIRMWARE_VERSION
#define FLOVA_FIRMWARE_VERSION "0.1.0"
#endif

// ESP32 board glue owns provisioning, board storage, and boot lifecycle. It
// keeps only credential metadata and the active generation number in RAM.
// Link configuration records are handled one at a time by
// the typed transport/installer; no JSON runtime snapshot is reconstructed.
class FlovaEsp32 : public FlovaDevice {
 public:
  FlovaEsp32()
      : FlovaDevice(transport_, storage_, clock_, logger_),
        configurationStorage_(storage_, kMaximumConfigurationRecords),
        configurationInstaller_(configurationStorage_, kMaximumConfigurationRecords) {}

  bool begin() {
    storage_.begin();
    const bool hasCredentials = loadCredentials();
    const bool hasPendingHandoff = loadPendingImage(provisioningImageWorkspace_);
    const flova::ProvisioningBootMode mode =
        flova::provisioningBootMode(hasCredentials, hasPendingHandoff,
                                    provisioningImageWorkspace_.inProgress);
    if (mode == flova::ProvisioningBootMode::InterruptedBootstrap) {
      storage_.setString("prov_error", "firmware_reset_during_bootstrap");
      storage_.remove("prov_pending");
      startProvisioningAp();
      return true;
    }
    if (mode == flova::ProvisioningBootMode::Bootstrap) {
      startBootstrap(provisioningImageWorkspace_.handoff);
      return true;
    }
    if (mode == flova::ProvisioningBootMode::Setup) {
      startProvisioningAp();
      return true;
    }
    storage_.remove("prov_pending");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(config_.wifiSsid, config_.wifiPassword);
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    runtimePreparing_ = true;
    return true;
  }

  void loop() {
    if (runtimePreparing_) {
      if (WiFi.status() == WL_CONNECTED && time(nullptr) >= kMinimumTlsEpoch)
        finishRuntimeStart();
      return;
    }
    if (provisioning_) {
      server_.handleClient();
      if (handoffPending_ && millis() - handoffAcceptedAt_ >= kProvisioningResponseGraceMs) {
        handoffPending_ = false;
        provisioning_ = false;
        server_.stop();
        WiFi.softAPdisconnect(true);
        Serial.println("[flova] provisioning handoff saved; rebooting for bootstrap");
        delay(100);
        ESP.restart();
      }
      return;
    }
    if (bootstrapPreparing_) {
      if (WiFi.status() == WL_CONNECTED && time(nullptr) >= kMinimumTlsEpoch) {
        Serial.printf("[flova] TLS clock synchronized epoch=%lu\n",
                      static_cast<unsigned long>(time(nullptr)));
        finishBootstrapStart();
      } else if (millis() - bootstrapStartedAt_ >= kBootstrapPreparationTimeoutMs) {
        failBootstrap(WiFi.status() == WL_CONNECTED ? "clock_sync_failed" : "wifi_or_url_failed");
      }
      return;
    }
    if (bootstrapActive_) {
      transport_.loop();
      applyPendingConfiguration();
      char errorCode[flova::kProvisioningErrorBytes] = {};
      if (transport_.takeBootstrapError(errorCode, sizeof(errorCode))) {
        Serial.println("[flova] bootstrap rejected; returning to setup AP");
        failBootstrap(errorCode[0] ? errorCode : "bootstrap_rejected", true);
        return;
      }
      if (!transport_.connected() && millis() - bootstrapStartedAt_ >= kBootstrapAttemptTimeoutMs) {
        Serial.println("[flova] bootstrap timeout");
        failBootstrap("bootstrap_timeout");
      }
      return;
    }
    if (runtimeReady_) FlovaDevice::loop();
  }

 protected:
  bool applyConfigurationRecord(const FlovaLinkConfigurationRecord& record) override {
    return applyConfiguration(record);
  }

  bool restoreActiveConfiguration() override {
    uint32_t newest = 0, previous = 0;
    configurationStorage_.generations(newest, previous);
    const uint32_t candidates[2] = {newest, previous};
    for (uint8_t candidate = 0; candidate < 2; ++candidate) {
      const uint32_t generation = candidates[candidate];
      if (!generation) continue;
      if (!validateConfigurationGeneration(generation)) {
        configurationStorage_.discardGeneration(generation);
        continue;
      }
      if (!applyConfigurationGeneration(generation)) {
        configurationStorage_.discardGeneration(generation);
        delay(20);
        ESP.restart();
        return false;
      }
      setActiveConfiguration(generation, restoreManifestWorkspace_.checksum.bytes);
      return true;
    }
    static const uint8_t emptyChecksum[32] = {};
    setActiveConfiguration(0, emptyChecksum, false);
    Serial.println("[flova] no valid runtime configuration; recovery sync enabled");
    return true;
  }

  void onConfigurationCommitted(uint32_t generation) override {
    Serial.printf("[flova] configuration generation=%lu committed; rebooting cleanly\n",
                  static_cast<unsigned long>(generation));
    transport_.disconnect();
    delay(100);
    ESP.restart();
  }

  void onBootstrapCommitted(const FlovaLinkBootstrapCommitted& committed) override {
    flova::DeviceConfiguration& final = credentialWorkspace_;
    final = flova::DeviceConfiguration();
    if (!uuidText(committed.deviceId, final.deviceId, sizeof(final.deviceId)) ||
        !flova::copyBounded(pending_.wifiSsid, final.wifiSsid) ||
        !flova::copyBounded(pending_.wifiPassword, final.wifiPassword) ||
        !flova::copyBounded(pending_.linkUrl, final.linkUrl) ||
        !flova::copyBounded(pending_.linkSecret, final.linkSecret)) {
      failBootstrap("credential_storage_failed");
      return;
    }
    final.generation = committed.generation;
    if (!storage_.writeConfig(final)) {
      failBootstrap("credential_storage_failed");
      return;
    }
    Serial.printf("[flova] bootstrap committed generation=%lu; rebooting into runtime\n",
                  static_cast<unsigned long>(committed.generation));
    storage_.remove("prov_pending");
    storage_.remove("prov_error");
    bootstrapActive_ = false;
    if (bootstrapTransportReady_) transport_.disconnect();
    delay(100);
    ESP.restart();
  }

 private:
  void finishRuntimeStart() {
    runtimePreparing_ = false;
    Serial.printf("[flova] TLS clock synchronized epoch=%lu\n",
                  static_cast<unsigned long>(time(nullptr)));
    if (!transport_.configure(config_.linkUrl)) {
      Serial.println("[flova] runtime Link URL invalid; staying offline");
      return;
    }
    FlovaConfig deviceConfig;
    deviceConfig.deviceId = config_.deviceId;
    deviceConfig.linkSecret = config_.linkSecret;
    deviceConfig.firmwareVersion = FLOVA_FIRMWARE_VERSION;
    deviceConfig.firmwareTarget = "universal_esp32";
    deviceConfig.boardType = "esp32";
    deviceConfig.otaCapable = true;
    deviceConfig.rollbackCapable = bootControl_.strategy() != FlovaOtaStrategy::None;
    deviceConfig.flashSize = ESP.getFlashChipSize();
    deviceConfig.capabilities.datastreamSlots = FLOVA_DATASTREAM_CAPACITY;
    deviceConfig.capabilities.hardwareInputSlots = FLOVA_HARDWARE_INPUT_CAPACITY;
    deviceConfig.capabilities.hardwareOutputSlots = FLOVA_HARDWARE_OUTPUT_CAPACITY;
    deviceConfig.capabilities.commandDedupSlots = FLOVA_COMMAND_DEDUP_CAPACITY;
    deviceConfig.capabilities.scheduleSlots = FLOVA_SCHEDULE_RUNTIME_ENABLED ? FLOVA_SCHEDULE_CAPACITY : 0;
    deviceConfig.capabilities.messageBytes = flova::link::kMaximumFrameBytes;
    deviceConfig.limits.messageBytes = flova::link::kMaximumFrameBytes;
    deviceConfig.appliedTemplateVersionId = config_.templateVersionId;
    deviceConfig.configChecksum = config_.checksum;
    configure(deviceConfig);
    setOtaInstaller(otaInstaller_);
    setBootControl(bootControl_);
    runtimeReady_ = beginTransportOnly();
  }

  bool validateConfigurationGeneration(uint32_t generation) {
    restoreManifestWorkspace_ = flova::config::GenerationManifest();
    if (!configurationStorage_.generationManifest(generation, restoreManifestWorkspace_) ||
        !restoreManifestWorkspace_.finalized ||
        restoreManifestWorkspace_.recordCount > kMaximumConfigurationRecords) return false;
    flova::config::Digest digest;
    for (uint32_t sequence = 0; sequence < restoreManifestWorkspace_.recordCount; ++sequence) {
      configurationRecordWorkspace_ = flova::config::Record();
      configurationDecodeWorkspace_ = FlovaLinkConfigurationRecord();
      if (!configurationStorage_.readRecord(generation, sequence, configurationRecordWorkspace_) ||
          !transport_.decodeStoredConfigurationRecord(configurationRecordWorkspace_.body,
                                                       configurationRecordWorkspace_.length,
                                                       configurationDecodeWorkspace_) ||
          configurationDecodeWorkspace_.generation != generation ||
          configurationDecodeWorkspace_.sequence != sequence) return false;
      digest.addRecord(configurationRecordWorkspace_);
    }
    flova::config::Checksum actual;
    digest.finish(actual);
    return actual.equals(restoreManifestWorkspace_.checksum);
  }

  bool applyConfigurationGeneration(uint32_t generation) {
    setConfigurationApplyingGeneration(generation);
    for (uint32_t sequence = 0; sequence < restoreManifestWorkspace_.recordCount; ++sequence) {
      configurationRecordWorkspace_ = flova::config::Record();
      configurationDecodeWorkspace_ = FlovaLinkConfigurationRecord();
      if (!configurationStorage_.readRecord(generation, sequence, configurationRecordWorkspace_) ||
          !transport_.decodeStoredConfigurationRecord(configurationRecordWorkspace_.body,
                                                       configurationRecordWorkspace_.length,
                                                       configurationDecodeWorkspace_) ||
          !applyConfigurationUnit(configurationDecodeWorkspace_.typedUnit)) return false;
    }
    return true;
  }
  bool loadCredentials() {
    credentialWorkspace_ = flova::DeviceConfiguration();
    if (!storage_.readConfig(credentialWorkspace_)) return false;
    config_ = credentialWorkspace_;
    return flova::configurationValid(config_);
  }

  void startProvisioningAp() {
    provisioning_ = true;
    runtimeReady_ = false;
    bootstrapActive_ = false;
    bootstrapPreparing_ = false;
    bootstrapTransportReady_ = false;
    handoffPending_ = false;
    WiFi.mode(WIFI_AP);
    char ssid[32] = {};
    snprintf(ssid, sizeof(ssid), "Flova-Setup-%08lx", static_cast<unsigned long>(ESP.getEfuseMac()));
    WiFi.softAP(ssid);
    if (!serverRoutesRegistered_) {
      server_.on("/status", HTTP_GET, [this]() {
        Serial.println("[flova] GET /status");
        String body = "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"can_retry\":true";
        String error;
        if (storage_.getString("prov_error", error)) {
          char safeError[flova::kProvisioningErrorBytes] = {};
          flova::sanitizeProvisioningError(error.c_str(), safeError);
          body += ",\"last_error_code\":\"" + String(safeError) + "\"";
        }
        body += "}";
        server_.send(200, "application/json", body);
      });
      server_.on("/provision", HTTP_POST, [this]() { handleProvision(); });
      serverRoutesRegistered_ = true;
    }
    server_.begin();
  }

  void handleProvision() {
    String body = server_.arg("plain");
    pending_ = flova::ProvisioningHandoff();
    if (body.length() >= 768 || !flova::parseProvisioningHandoff(body.c_str(), body.length(), pending_) ||
        !flova::generateSecret(pending_.linkSecret)) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_handoff\"}");
      return;
    }
    provisioningImageWorkspace_ = flova::ProvisioningHandoffImage();
    flova::makeProvisioningImage(pending_, provisioningImageWorkspace_);
    if (!storage_.write("prov_pending", &provisioningImageWorkspace_, sizeof(provisioningImageWorkspace_))) {
      server_.send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
      return;
    }
    storage_.remove("prov_error");
    server_.send(202, "application/json", "{\"ok\":true,\"status\":\"accepted\"}");
    handoffPending_ = true;
    handoffAcceptedAt_ = millis();
  }

  bool loadPendingImage(flova::ProvisioningHandoffImage& image) {
    return storage_.read("prov_pending", &image, sizeof(image)) &&
           flova::verifyProvisioningImage(image);
  }

  void startBootstrap(const flova::ProvisioningHandoff& handoff) {
    if (!loadPendingImage(provisioningImageWorkspace_)) {
      storage_.setString("prov_error", "invalid_handoff");
      startProvisioningAp();
      return;
    }
    if (provisioningImageWorkspace_.attempts >= kMaximumBootstrapAttempts) {
      Serial.println("[flova] bootstrap stopped; returning to setup AP");
      storage_.remove("prov_pending");
      storage_.setString("prov_error", "bootstrap_attempts_exhausted");
      startProvisioningAp();
      return;
    }
    pending_ = handoff;
    flova::markProvisioningAttempt(provisioningImageWorkspace_);
    if (!storage_.write("prov_pending", &provisioningImageWorkspace_, sizeof(provisioningImageWorkspace_))) {
      storage_.setString("prov_error", "storage_failed");
      storage_.remove("prov_pending");
      startProvisioningAp();
      return;
    }
    bootstrapActive_ = false;
    bootstrapPreparing_ = false;
    if (bootstrapTransportReady_) transport_.disconnect();
    if (!transport_.configure(pending_.linkUrl)) {
      failBootstrap("wifi_or_url_failed");
      return;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(pending_.wifiSsid, pending_.wifiPassword);
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    bootstrapPreparing_ = true;
    bootstrapStartedAt_ = millis();
  }

  void finishBootstrapStart() {
    bootstrapPreparing_ = false;
    FlovaConfig deviceConfig;
    deviceConfig.linkSecret = pending_.linkSecret;
    deviceConfig.firmwareVersion = FLOVA_FIRMWARE_VERSION;
    deviceConfig.firmwareTarget = "universal_esp32";
    deviceConfig.boardType = "esp32";
    deviceConfig.capabilities.messageBytes = flova::link::kMaximumFrameBytes;
    deviceConfig.limits.messageBytes = flova::link::kMaximumFrameBytes;
    configure(deviceConfig);
    deferConfigurationRuntime(true);
    if (!bootstrapTransportReady_ && !beginTransportOnly()) {
      Serial.println("[flova] bootstrap transport setup failed");
      failBootstrap("transport_setup_failed");
      return;
    }
    bootstrapTransportReady_ = true;
    char hardwareId[48] = {};
    snprintf(hardwareId, sizeof(hardwareId), "esp32-%08lx", static_cast<unsigned long>(ESP.getEfuseMac()));
    if (!transport_.connectBootstrap(pending_.token, hardwareId, "universal_esp32", pending_.linkSecret)) {
      Serial.println("[flova] bootstrap transport connect failed");
      failBootstrap("tls_connect_failed");
      return;
    }
    bootstrapActive_ = true;
    bootstrapStartedAt_ = millis();
  }

  void failBootstrap(const char* errorCode, bool terminal = false) {
    if (bootstrapTransportReady_) transport_.disconnect();
    bootstrapActive_ = false;
    bootstrapPreparing_ = false;
    char safeError[flova::kProvisioningErrorBytes] = {};
    flova::sanitizeProvisioningError(errorCode, safeError);
    storage_.setString("prov_error", safeError);
    if (loadPendingImage(provisioningImageWorkspace_)) {
      flova::markProvisioningFailure(provisioningImageWorkspace_, safeError);
      storage_.write("prov_pending", &provisioningImageWorkspace_, sizeof(provisioningImageWorkspace_));
    }
    if (terminal || !loadPendingImage(provisioningImageWorkspace_) ||
        provisioningImageWorkspace_.attempts >= kMaximumBootstrapAttempts) {
      if (terminal) Serial.println("[flova] bootstrap stopped; returning to setup AP");
      else Serial.println("[flova] bootstrap attempts exhausted");
      storage_.remove("prov_pending");
    }
    delay(100);
    ESP.restart();
  }

  static bool uuidText(const FlovaLinkId& id, char* output, size_t capacity) {
    if (!id.present || capacity < 37) return false;
    static const char hex[] = "0123456789abcdef";
    size_t out = 0;
    for (size_t i = 0; i < 16; ++i) {
      if (i == 4 || i == 6 || i == 8 || i == 10) output[out++] = '-';
      output[out++] = hex[id.bytes[i] >> 4];
      output[out++] = hex[id.bytes[i] & 15];
    }
    output[out] = 0;
    return true;
  }

  bool applyConfiguration(const FlovaLinkConfigurationRecord& input) {
    if (input.phase == FlovaLinkConfigurationPhase::Begin) {
      flova::config::Begin begin;
      begin.messageId = input.messageId;
      begin.generation = input.generation;
      begin.schemaVersion = input.schemaVersion;
      begin.maximumRecordBytes = input.maximumRecordBytes;
      begin.recordCount = input.recordCount;
      memcpy(begin.checksum.bytes, input.checksum, sizeof(input.checksum));
      return configurationInstaller_.begin(begin).accepted();
    }
    if (input.phase == FlovaLinkConfigurationPhase::Record) {
      flova::config::Record& record = configurationRecordWorkspace_;
      record = flova::config::Record();
      record.messageId = input.messageId;
      record.generation = input.generation;
      record.sequence = input.sequence;
      record.kind = static_cast<flova::config::RecordKind>(input.recordType);
      record.length = input.recordLength;
      if (record.length > sizeof(record.body)) return false;
      memcpy(record.body, input.record, record.length);
      const flova::config::Ack ack = configurationInstaller_.record(record);
      if (!ack.accepted()) return false;
      return true;
    }
    flova::config::End end;
    end.messageId = input.messageId;
    end.generation = input.generation;
    end.recordCount = input.recordCount;
    memcpy(end.checksum.bytes, input.checksum, sizeof(input.checksum));
    return configurationInstaller_.end(end).accepted();
  }

  static const uint32_t kMaximumConfigurationRecords =
      FLOVA_DATASTREAM_CAPACITY + FLOVA_SCHEDULE_CAPACITY + 8;

  class Storage : public ArduinoStorage {
   public:
    void begin() { preferences_.begin("flova", false); }
    bool read(const char* key, void* output, size_t size) const override {
      return key && output && size && preferences_.getBytesLength(key) == size &&
             preferences_.getBytes(key, output, size) == size;
    }
    bool write(const char* key, const void* value, size_t size) override {
      return key && value && size && preferences_.putBytes(key, value, size) == size;
    }
    bool readConfig(flova::DeviceConfiguration& out) const {
      flova::ConfigurationImage image = {};
      if (preferences_.getBytesLength("config") != sizeof(image) ||
          preferences_.getBytes("config", &image, sizeof(image)) != sizeof(image) ||
          !flova::verifyConfigurationImage(image)) return false;
      out = image.configuration;
      return true;
    }
    bool writeConfig(const flova::DeviceConfiguration& config) {
      if (!flova::configurationValid(config)) return false;
      flova::ConfigurationImage image = {};
      flova::makeConfigurationImage(config, image);
      return preferences_.putBytes("config", &image, sizeof(image)) == sizeof(image) && readConfig(verified_);
    }
    bool getString(const char* key, String& out) override { out = preferences_.getString(key, ""); return out.length() > 0; }
    bool getString(const char* key, char* out, size_t maxLen) override {
      if (!out || !maxLen) return false;
      String value;
      if (!getString(key, value) || value.length() >= maxLen) return false;
      memcpy(out, value.c_str(), value.length() + 1);
      return true;
    }
    bool setString(const char* key, const char* value) override { return preferences_.putString(key, value ? value : "") > 0; }
    bool remove(const char* key) override { return preferences_.remove(key); }
    void clear() override { preferences_.clear(); }
   private:
    mutable Preferences preferences_;
    mutable flova::DeviceConfiguration verified_ = {};
  } storage_;

  FlovaLinkConfigurationStorage configurationStorage_;
  flova::config::Installer configurationInstaller_;
  flova::config::Record configurationRecordWorkspace_ = {};
  FlovaLinkConfigurationRecord configurationDecodeWorkspace_ = {};
  flova::config::GenerationManifest restoreManifestWorkspace_ = {};

  ArduinoDeviceLink transport_;
  ArduinoClock clock_;
  ArduinoLogger logger_;
  ArduinoOtaInstaller otaInstaller_;
  FlovaLegacyBootControl bootControl_;
  WebServer server_{80};
  flova::DeviceConfiguration config_ = {};
  flova::DeviceConfiguration credentialWorkspace_ = {};
  flova::ProvisioningHandoff pending_ = {};
  flova::ProvisioningHandoffImage provisioningImageWorkspace_ = {};
  bool provisioning_ = false;
  bool runtimeReady_ = false;
  bool runtimePreparing_ = false;
  bool bootstrapActive_ = false;
  bool bootstrapPreparing_ = false;
  bool handoffPending_ = false;
  bool serverRoutesRegistered_ = false;
  bool bootstrapTransportReady_ = false;
  uint32_t bootstrapStartedAt_ = 0;
  uint32_t handoffAcceptedAt_ = 0;
  static const uint8_t kMaximumBootstrapAttempts = 3;
  static const time_t kMinimumTlsEpoch = 1700000000;
  static const uint32_t kBootstrapPreparationTimeoutMs = 30000;
  static const uint32_t kBootstrapAttemptTimeoutMs = 30000;
  static const uint32_t kProvisioningResponseGraceMs = 1000;
};
