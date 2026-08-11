#pragma once

#include <new>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <time.h>
#include <FlovaArduino.h>
#include <FlovaConfiguration.h>
#include <FlovaLinkCodec.h>
#include <FlovaLinkConfigurationStorage.h>
#include <FlovaProvisioningHandoff.h>
#include <FlovaTlsProfile.h>
#include <FlovaTlsRoots.h>

extern "C" {
#include <user_interface.h>
}

#ifndef FLOVA_FIRMWARE_VERSION
#define FLOVA_FIRMWARE_VERSION "0.1.0"
#endif

// ESP8266 board glue owns SoftAP/bootstrap, TLS resource handoff, and board
// storage. It uses fixed credential images plus the existing bounded state slots.
// Engine configuration is never represented as JSON or retained as one RAM
// object; the verified WSS transaction supplies one schema-defined record at a
// time to the board installer.
class FlovaEsp8266 : public FlovaDevice {
 public:
  FlovaEsp8266()
      : FlovaDevice(transport_, storage_, clock_, logger_),
        imageWorkspace_(),
        storage_(imageWorkspace_.configuration()),
        configurationStorage_(storage_, kMaximumConfigurationRecords),
        configurationInstaller_(configurationStorage_, kMaximumConfigurationRecords) {
  }

  bool begin() {
    storage_.begin();
    if (const rst_info* reset = system_get_rst_info()) {
      Serial.printf(
          "[flova] reset reason=%u(%s) exccause=%u epc1=0x%08lx depc=0x%08lx excvaddr=0x%08lx\n",
          static_cast<unsigned>(reset->reason),
          resetReasonName(reset->reason),
          static_cast<unsigned>(reset->exccause),
          static_cast<unsigned long>(reset->epc1),
          static_cast<unsigned long>(reset->depc),
          static_cast<unsigned long>(reset->excvaddr));
    }
    const bool hasCredentials = loadCredentials();
    imageWorkspace_.useProvisioning();
    const bool hasPendingHandoff = loadPendingImage(imageWorkspace_.provisioning());
    const flova::ProvisioningBootMode mode =
        flova::provisioningBootMode(hasCredentials, hasPendingHandoff,
                                    imageWorkspace_.provisioning().inProgress);
    if (mode == flova::ProvisioningBootMode::InterruptedBootstrap) {
      storage_.setString("prov_error", "firmware_reset_during_bootstrap");
      storage_.remove("prov_pending");
      startProvisioningAp();
      return true;
    }
    if (mode == flova::ProvisioningBootMode::Bootstrap) {
      startBootstrap(imageWorkspace_.provisioning().handoff);
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
      const uint8_t stationCount = WiFi.softAPgetStationNum();
      if (stationCount != apStationCount_) {
        apStationCount_ = stationCount;
        Serial.printf("[flova] setup AP stations=%u heap=%u max_block=%u frag=%u%%\n",
                      static_cast<unsigned>(stationCount),
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(ESP.getMaxFreeBlockSize()),
                      static_cast<unsigned>(ESP.getHeapFragmentation()));
      }
      // ESP8266WebServer writes the response after the handler returns. Do
      // not tear down the SoftAP from inside handleProvision(), or the phone
      // receives a connection failure instead of the 202 acceptance.
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
      if (!transport_.connected() &&
          millis() - bootstrapStartedAt_ >= kBootstrapAttemptTimeoutMs) {
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
    Serial.printf("[flova] runtime restore candidates newest=%lu previous=%lu\n",
                  static_cast<unsigned long>(newest),
                  static_cast<unsigned long>(previous));
    const uint32_t candidates[2] = {newest, previous};
    for (uint8_t candidate = 0; candidate < 2; ++candidate) {
      const uint32_t generation = candidates[candidate];
      if (!generation) continue;
      Serial.printf("[flova] runtime restore validate generation=%lu\n",
                    static_cast<unsigned long>(generation));
      if (!validateConfigurationGeneration(generation)) {
        Serial.printf("[flova] configuration generation=%lu corrupt; discarding\n",
                      static_cast<unsigned long>(generation));
        configurationStorage_.discardGeneration(generation);
        continue;
      }
      Serial.printf("[flova] runtime restore apply generation=%lu\n",
                    static_cast<unsigned long>(generation));
      if (!applyConfigurationGeneration(generation)) {
        configurationStorage_.discardGeneration(generation);
        Serial.printf("[flova] configuration generation=%lu apply failed; recovering without reboot\n",
                      static_cast<unsigned long>(generation));
        continue;
      }
      setActiveConfiguration(generation, restoreManifestWorkspace_.checksum.bytes);
      Serial.printf("[flova] runtime restore applied generation=%lu\n",
                    static_cast<unsigned long>(generation));
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

  void onRuntimeRestoreBegin() override {
    char marker[48] = {};
    const bool interrupted = storage_.getString("prov_error", marker, sizeof(marker)) &&
                             strcmp(marker, "runtime_restore_in_progress") == 0;
    if (interrupted) {
      Serial.println("[flova] interrupted runtime restore; quarantining newest generation");
      configurationStorage_.discardNewestGeneration();
    }
    storage_.setString("prov_error", "runtime_restore_in_progress");
    Serial.println("[flova] runtime configuration restore begin");
  }

  void onRuntimeRestoreComplete(bool restored) override {
    if (restored) storage_.remove("prov_error");
    Serial.printf("[flova] runtime configuration restore complete status=%u generation=%lu\n",
                  restored ? 0U : 1U,
                  static_cast<unsigned long>(activeConfigurationGeneration()));
  }

  void onBootstrapCommitted(const FlovaLinkBootstrapCommitted& committed) override {
    imageWorkspace_.useConfiguration();
    flova::DeviceConfiguration& final = config_;
    final = flova::DeviceConfiguration();
    if (!uuidText(committed.deviceId, final.deviceId, sizeof(final.deviceId)) ||
        !flova::copyBounded(pending_.wifiSsid, final.wifiSsid) ||
        !flova::copyBounded(pending_.wifiPassword, final.wifiPassword) ||
        !flova::copyBounded(pending_.linkUrl, final.linkUrl) ||
        !flova::copyBounded(pending_.linkSecret, final.linkSecret)) {
      imageWorkspace_.useProvisioning();
      failBootstrap("credential_storage_failed");
      return;
    }
    final.generation = committed.generation;
    if (!storage_.writeConfig(final)) {
      imageWorkspace_.useProvisioning();
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
  static const char* resetReasonName(uint8_t reason) {
    switch (reason) {
      case 2: return "exception";
      case 3: return "soft_watchdog";
      case 4: return "software_restart";
      case 6: return "external_manual";
      default: return "other";
    }
  }

  void finishRuntimeStart() {
    runtimePreparing_ = false;
    Serial.printf("[flova] TLS clock synchronized epoch=%lu\n",
                  static_cast<unsigned long>(time(nullptr)));
    if (!ensureTrustAnchors()) {
      Serial.println("[flova] TLS trust anchors unavailable; staying offline");
      return;
    }
    if (!transport_.configure(config_.linkUrl)) {
      Serial.println("[flova] runtime Link URL invalid; staying offline");
      return;
    }
    FlovaConfig deviceConfig;
    deviceConfig.deviceId = config_.deviceId;
    deviceConfig.linkSecret = config_.linkSecret;
    deviceConfig.firmwareVersion = FLOVA_FIRMWARE_VERSION;
    deviceConfig.firmwareTarget = "universal_esp8266";
    deviceConfig.boardType = "esp8266";
    deviceConfig.otaCapable = true;
    deviceConfig.flashSize = ESP.getFlashChipRealSize();
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
    // Valid credentials make this a runtime boot. Wi-Fi, SNTP, and Link all
    // recover through their native retry paths; none may reopen setup mode.
    runtimeReady_ = beginTransportOnly();
  }

  bool validateConfigurationGeneration(uint32_t generation) {
    restoreManifestWorkspace_ = flova::config::GenerationManifest();
    if (!configurationStorage_.generationManifest(generation, restoreManifestWorkspace_) ||
        !restoreManifestWorkspace_.finalized ||
        restoreManifestWorkspace_.recordCount > kMaximumConfigurationRecords) return false;
    flova::config::Digest digest;
    for (uint32_t sequence = 0; sequence < restoreManifestWorkspace_.recordCount; ++sequence) {
      Serial.printf("[flova] runtime restore validate record=%lu\n",
                    static_cast<unsigned long>(sequence));
      if (!configurationInstaller_.loadWorkspace(generation, sequence)) return false;
      const flova::config::Record& record = configurationInstaller_.workspace();
      if (record.generation != generation || record.sequence != sequence) return false;
      digest.addRecord(record);
      optimistic_yield(1000);
    }
    flova::config::Checksum actual;
    digest.finish(actual);
    return actual.equals(restoreManifestWorkspace_.checksum);
  }

  bool applyConfigurationGeneration(uint32_t generation) {
    setConfigurationApplyingGeneration(generation);
    flova::config::Unit unit = {};
    for (uint32_t sequence = 0; sequence < restoreManifestWorkspace_.recordCount; ++sequence) {
      Serial.printf("[flova] runtime restore decode record=%lu\n",
                    static_cast<unsigned long>(sequence));
      if (!configurationInstaller_.loadWorkspace(generation, sequence)) return false;
      const flova::config::Record& record = configurationInstaller_.workspace();
      if (!transport_.decodeStoredConfigurationUnit(record.body, record.length, unit) ||
          record.generation != generation || record.sequence != sequence ||
          !applyConfigurationUnit(unit)) return false;
      Serial.printf("[flova] runtime restore applied record=%lu\n",
                    static_cast<unsigned long>(sequence));
      optimistic_yield(1000);
    }
    return true;
  }
  bool loadCredentials() {
    config_ = flova::DeviceConfiguration();
    if (!storage_.readConfig(config_)) return false;
    return flova::configurationValid(config_);
  }

  void startProvisioningAp() {
    provisioning_ = true;
    runtimeReady_ = false;
    bootstrapActive_ = false;
    bootstrapPreparing_ = false;
    bootstrapTransportReady_ = false;
    handoffPending_ = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_AP);
    const IPAddress apIp(192, 168, 4, 1);
    const IPAddress apSubnet(255, 255, 255, 0);
    const bool apConfigOk = WiFi.softAPConfig(apIp, apIp, apSubnet);
    char ssid[32] = {};
    snprintf(ssid, sizeof(ssid), "Flova-Setup-%06lx", static_cast<unsigned long>(ESP.getChipId()));
    const bool apStarted = WiFi.softAP(ssid, nullptr, 1, false, 4);
    const IPAddress activeApIp = WiFi.softAPIP();
    apStationCount_ = 0;
    Serial.printf("[flova] setup AP config=%u start=%u ip=%u.%u.%u.%u heap=%u max_block=%u frag=%u%%\n",
                  apConfigOk ? 1U : 0U,
                  apStarted ? 1U : 0U,
                  activeApIp[0], activeApIp[1], activeApIp[2], activeApIp[3],
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxFreeBlockSize()),
                  static_cast<unsigned>(ESP.getHeapFragmentation()));
    if (!apConfigOk || !apStarted) {
      Serial.println("[flova] setup AP failed; restarting");
      provisioning_ = false;
      delay(100);
      ESP.restart();
      return;
    }
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
    imageWorkspace_.provisioning() = flova::ProvisioningHandoffImage();
    flova::makeProvisioningImage(pending_, imageWorkspace_.provisioning());
    if (!storage_.write("prov_pending", &imageWorkspace_.provisioning(), sizeof(imageWorkspace_.provisioning()))) {
      server_.send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
      return;
    }
    storage_.remove("prov_error");
    server_.send(202, "application/json", "{\"ok\":true,\"status\":\"accepted\"}");
    server_.client().flush(1000);
    Serial.println("[flova] provisioning accepted");
    handoffPending_ = true;
    handoffAcceptedAt_ = millis();
  }

  bool loadPendingImage(flova::ProvisioningHandoffImage& image) {
    return storage_.read("prov_pending", &image, sizeof(image)) &&
           flova::verifyProvisioningImage(image);
  }

  void startBootstrap(const flova::ProvisioningHandoff& handoff) {
    if (!loadPendingImage(imageWorkspace_.provisioning())) {
      storage_.setString("prov_error", "invalid_handoff");
      startProvisioningAp();
      return;
    }
    if (imageWorkspace_.provisioning().attempts >= kMaximumBootstrapAttempts) {
      Serial.println("[flova] bootstrap stopped; returning to setup AP");
      storage_.remove("prov_pending");
      storage_.setString("prov_error", "bootstrap_attempts_exhausted");
      startProvisioningAp();
      return;
    }
    pending_ = handoff;
    flova::markProvisioningAttempt(imageWorkspace_.provisioning());
    if (!storage_.write("prov_pending", &imageWorkspace_.provisioning(), sizeof(imageWorkspace_.provisioning()))) {
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
    if (!ensureTrustAnchors()) {
      Serial.println("[flova] bootstrap TLS trust anchors unavailable");
      failBootstrap("tls_memory_unavailable");
      return;
    }
    FlovaConfig deviceConfig;
    deviceConfig.linkSecret = pending_.linkSecret;
    deviceConfig.firmwareVersion = FLOVA_FIRMWARE_VERSION;
    deviceConfig.firmwareTarget = "universal_esp8266";
    deviceConfig.boardType = "esp8266";
    deviceConfig.capabilities.messageBytes = flova::link::kMaximumFrameBytes;
    deviceConfig.limits.messageBytes = flova::link::kMaximumFrameBytes;
    configure(deviceConfig);
    // Bootstrap only persists the transactional configuration. Runtime String
    // objects and hardware mappings are restored after commit on a clean boot,
    // when BearSSL is not competing with the configuration installer.
    deferConfigurationRuntime(true);
    if (!bootstrapTransportReady_ && !beginTransportOnly()) {
      Serial.println("[flova] bootstrap transport setup failed");
      failBootstrap("transport_setup_failed");
      return;
    }
    bootstrapTransportReady_ = true;
    char hardwareId[48] = {};
    snprintf(hardwareId, sizeof(hardwareId), "esp8266-%06lx", static_cast<unsigned long>(ESP.getChipId()));
    if (!transport_.connectBootstrap(pending_.token, hardwareId, "universal_esp8266", pending_.linkSecret)) {
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
    if (loadPendingImage(imageWorkspace_.provisioning())) {
      flova::markProvisioningFailure(imageWorkspace_.provisioning(), safeError);
      storage_.write("prov_pending", &imageWorkspace_.provisioning(), sizeof(imageWorkspace_.provisioning()));
    }
    if (terminal || !loadPendingImage(imageWorkspace_.provisioning()) ||
        imageWorkspace_.provisioning().attempts >= kMaximumBootstrapAttempts) {
      if (terminal) Serial.println("[flova] bootstrap stopped; returning to setup AP");
      else Serial.println("[flova] bootstrap attempts exhausted");
      storage_.remove("prov_pending");
    }
    // Every retry starts from a fresh heap. This also guarantees the setup
    // HTTP server and BearSSL are never alive in the same boot lifecycle.
    delay(100);
    ESP.restart();
  }

  bool ensureTrustAnchors() {
    if (trustAnchors_) return true;
    BearSSL::X509List* anchors =
        new (std::nothrow) BearSSL::X509List(FLOVA_TLS_ROOT_CERTS);
    if (!anchors || !anchors->getCount()) {
      delete anchors;
      return false;
    }
    trustAnchors_ = anchors;
    transport_.setTrustAnchors(*trustAnchors_);
    otaInstaller_.setTrustAnchors(*trustAnchors_);
    return true;
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
      const flova::config::Ack ack = configurationInstaller_.begin(begin);
      Serial.printf("[flova] configuration begin status=%u records=%lu\n",
                    static_cast<unsigned>(ack.status),
                    static_cast<unsigned long>(input.recordCount));
      return ack.accepted();
    }
    if (input.phase == FlovaLinkConfigurationPhase::Record) {
      flova::config::Record& record = configurationInstaller_.workspace();
      record = flova::config::Record();
      record.messageId = input.messageId;
      record.generation = input.generation;
      record.sequence = input.sequence;
      record.kind = static_cast<flova::config::RecordKind>(input.recordType);
      record.length = input.recordLength;
      if (record.length > sizeof(record.body)) return false;
      memcpy(record.body, input.record, record.length);
      flova::logTlsHeap("before configuration record persistence");
      const flova::config::Ack ack = configurationInstaller_.record(record);
      flova::logTlsHeap("after configuration record persistence");
      Serial.printf("[flova] configuration record sequence=%lu status=%u bytes=%u\n",
                    static_cast<unsigned long>(input.sequence),
                    static_cast<unsigned>(ack.status),
                    static_cast<unsigned>(input.recordLength));
      if (!ack.accepted()) return false;
      return true;
    }
    flova::config::End end;
    end.messageId = input.messageId;
    end.generation = input.generation;
    end.recordCount = input.recordCount;
    memcpy(end.checksum.bytes, input.checksum, sizeof(input.checksum));
    const flova::config::Ack ack = configurationInstaller_.end(end);
    Serial.printf("[flova] configuration end status=%u records=%lu\n",
                  static_cast<unsigned>(ack.status),
                  static_cast<unsigned long>(input.recordCount));
    return ack.accepted();
  }

  static const uint32_t kMaximumConfigurationRecords =
      FLOVA_DATASTREAM_CAPACITY + FLOVA_SCHEDULE_CAPACITY + 8;

  union ImageWorkspace {
    flova::ConfigurationImage configurationImage;
    ImageWorkspace() : configurationImage() {}
    ~ImageWorkspace() {}

    flova::ConfigurationImage& configuration() {
      return configurationImage;
    }
    flova::ProvisioningHandoffImage& provisioning() {
      return provisioningImage;
    }
    void useConfiguration() {
      provisioningImage.~ProvisioningHandoffImage();
      new (&configurationImage) flova::ConfigurationImage();
    }
    void useProvisioning() {
      configurationImage.~ConfigurationImage();
      new (&provisioningImage) flova::ProvisioningHandoffImage();
    }
    flova::ProvisioningHandoffImage provisioningImage;
  };

  class Storage : public ArduinoStorage {
   public:
    explicit Storage(flova::ConfigurationImage& workspace)
        : configurationImageWorkspace_(workspace) {}

    void begin() {
      if (!LittleFS.begin()) Serial.println("[flova] LittleFS mount failed");
    }
    bool read(const char* key, void* output, size_t size) const override {
      char path[48] = {};
      if (!makePath(key, path, sizeof(path))) return false;
      File file = LittleFS.open(path, "r");
      if (!file) return false;
      if (static_cast<size_t>(file.size()) != size) {
        file.close();
        return false;
      }
      size_t read = 0;
      uint8_t* bytes = reinterpret_cast<uint8_t*>(output);
      while (read < size) {
        const size_t chunk = min<size_t>(64, size - read);
        const size_t count = file.read(bytes + read, chunk);
        if (count != chunk) break;
        read += count;
        optimistic_yield(1000);
      }
      const bool ok = read == size;
      file.close();
      optimistic_yield(1000);
      return ok;
    }
    bool write(const char* key, const void* value, size_t size) override {
      char path[48] = {}, next[48] = {};
      if (!makePath(key, path, sizeof(path)) ||
          snprintf(next, sizeof(next), "%s.next", path) >= static_cast<int>(sizeof(next))) return false;
      File file = LittleFS.open(next, "w");
      if (!file) return false;
      const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value);
      size_t written = 0;
      while (written < size) {
        const size_t chunk = min<size_t>(64, size - written);
        const size_t count = file.write(bytes + written, chunk);
        if (count != chunk) break;
        written += count;
        optimistic_yield(1000);
      }
      file.close();
      optimistic_yield(1000);
      if (written != size) {
        LittleFS.remove(next);
        return false;
      }
      LittleFS.remove(path);
      optimistic_yield(1000);
      return LittleFS.rename(next, path);
    }
    bool readConfig(flova::DeviceConfiguration& out) const {
      configurationImageWorkspace_ = flova::ConfigurationImage();
      if (!readImage("/config.active", configurationImageWorkspace_) || !flova::verifyConfigurationImage(configurationImageWorkspace_)) {
        if (!readImage("/config.backup", configurationImageWorkspace_) || !flova::verifyConfigurationImage(configurationImageWorkspace_)) return false;
      }
      out = configurationImageWorkspace_.configuration;
      return true;
    }
    bool writeConfig(const flova::DeviceConfiguration& config) {
      if (!flova::configurationValid(config)) return false;
      configurationImageWorkspace_ = flova::ConfigurationImage();
      flova::makeConfigurationImage(config, configurationImageWorkspace_);
      File next = LittleFS.open("/config.next", "w");
      if (!next) return false;
      const bool written = next.write(reinterpret_cast<const uint8_t*>(&configurationImageWorkspace_), sizeof(configurationImageWorkspace_)) == sizeof(configurationImageWorkspace_);
      next.close();
      if (!written) {
        LittleFS.remove("/config.next");
        return false;
      }
      LittleFS.remove("/config.backup");
      if (LittleFS.exists("/config.active")) LittleFS.rename("/config.active", "/config.backup");
      if (!LittleFS.rename("/config.next", "/config.active")) return false;
      flova::DeviceConfiguration verified = {};
      return readConfig(verified);
    }
    bool getString(const char* key, String& out) override {
      char value[kStringBytes] = {};
      if (!getString(key, value, sizeof(value))) {
        out = "";
        return false;
      }
      out = value;
      return true;
    }
    bool getString(const char* key, char* out, size_t maxLen) override {
      if (!out || !maxLen) return false;
      char path[48] = {};
      if (!makePath(key, path, sizeof(path))) return false;
      File file = LittleFS.open(path, "r");
      if (!file) return false;
      const size_t length = static_cast<size_t>(file.size());
      if (!length || length >= maxLen || length >= kStringBytes) {
        file.close();
        return false;
      }
      size_t read = 0;
      while (read < length) {
        const size_t chunk = min<size_t>(64, length - read);
        const size_t count = file.read(reinterpret_cast<uint8_t*>(out) + read, chunk);
        if (count != chunk) break;
        read += count;
        optimistic_yield(1000);
      }
      file.close();
      if (read != length) {
        out[0] = '\0';
        return false;
      }
      out[length] = '\0';
      return true;
    }
    bool setString(const char* key, const char* value) override {
      if (!value) value = "";
      size_t length = 0;
      while (value[length] && length < kStringBytes) ++length;
      if (length >= kStringBytes) return false;
      return write(key, value, length);
    }
    bool remove(const char* key) override {
      char path[48] = {};
      if (!makePath(key, path, sizeof(path))) return false;
      return !LittleFS.exists(path) || LittleFS.remove(path);
    }
    void clear() override { LittleFS.format(); }
   private:
    enum { kStringBytes = 224 };

    static bool makePath(const char* key, char* out, size_t size) {
      return key && out && snprintf(out, size, "/%s.bin", key) < static_cast<int>(size);
    }

    flova::ConfigurationImage& configurationImageWorkspace_;
    static bool readImage(const char* path, flova::ConfigurationImage& image) {
      File file = LittleFS.open(path, "r");
      if (!file) return false;
      if (file.size() != sizeof(image)) {
        file.close();
        return false;
      }
      const bool ok = file.read(reinterpret_cast<uint8_t*>(&image), sizeof(image)) == sizeof(image);
      file.close();
      return ok;
    }
  };

  ImageWorkspace imageWorkspace_ = {};
  Storage storage_;

  FlovaLinkConfigurationStorage configurationStorage_;
  flova::config::Installer configurationInstaller_;
  flova::config::GenerationManifest restoreManifestWorkspace_ = {};

  BearSSL::X509List* trustAnchors_ = nullptr;
  ArduinoDeviceLink transport_;
  ArduinoClock clock_;
  ArduinoLogger logger_;
  ArduinoOtaInstaller otaInstaller_;
  ESP8266WebServer server_{80};
  flova::DeviceConfiguration config_ = {};
  flova::ProvisioningHandoff pending_ = {};
  bool provisioning_ = false;
  uint8_t apStationCount_ = 0;
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
