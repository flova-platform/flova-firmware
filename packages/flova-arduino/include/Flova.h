#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <FlovaCore.h>
#include <FlovaConfiguration.h>
#include <FlovaClientLink.h>
#include <FlovaProvisioningHandoff.h>
#include <FlovaLinkConfigurationStorage.h>
#include <FlovaHardware.h>
#include <FlovaScheduleRuntime.h>
#include "adapters/ArduinoFlovaLink.h"
#include "adapters/ArduinoFlovaServices.h"
#include "FlovaBoardProvisioning.h"

#ifndef FLOVA_FIRMWARE_VERSION
#define FLOVA_FIRMWARE_VERSION "0.1.0"
#endif

struct FlovaClientConfig {
  const char* deviceId;
  const char* secret;
  const char* linkUrl;
};

struct FlovaProvisioningConfig {
  const char* wifiSsid;
  const char* wifiPassword;
  const char* hardwareId;
  const char* firmwareTarget;
  bool enabled;

  FlovaProvisioningConfig(const char* ssid = nullptr, const char* password = nullptr,
                          const char* hardware = nullptr,
                          const char* target = nullptr, bool allow = false)
      : wifiSsid(ssid), wifiPassword(password), hardwareId(hardware),
        firmwareTarget(target), enabled(allow) {}
};

enum class FlovaLifecycle : uint8_t {
  Idle,
  Setup,
  WaitingForWifi,
  Bootstrapping,
  Restarting,
  Runtime,
  Failed,
};

// One-header Arduino facade. Existing applications may continue to own Wi-Fi
// and pass the original three-field FlovaClientConfig. Adding a
// FlovaProvisioningConfig enables persisted credentials and phone setup.
class FlovaClient {
 public:
  FlovaClient(const FlovaClientConfig& config, FlovaClientLink& link,
              FlovaBoardProvisioning& board, flova::Storage& storage,
              flova::Clock& clock, flova::Logger& logger,
              FlovaEntropySource& entropy, flova::Hardware& hardware)
      : config_(config), provisioningConfig_(), link_(link), board_(board),
        storage_(storage), clock_(clock), logger_(logger), entropy_(entropy),
        configurationStorage_(storage_, kMaximumConfigurationRecords),
        configurationInstaller_(configurationStorage_, kMaximumConfigurationRecords),
        hardware_(hardware), device_(link_, storage_, clock_, logger_),
        scheduleRuntime_(storage_, clock_),
        scheduleCompiler_(scheduleRuntime_.workspace()) {
    hardware_.attach(device_);
    scheduleRuntime_.handlers(applyScheduledWrite, requestScheduleRenewal,
                              reportScheduleStatus, this);
  }

  FlovaClient(const FlovaClientConfig& config,
              const FlovaProvisioningConfig& provisioning,
              FlovaClientLink& link, FlovaBoardProvisioning& board,
              flova::Storage& storage, flova::Clock& clock,
              flova::Logger& logger, FlovaEntropySource& entropy,
              flova::Hardware& hardware)
      : config_(config), provisioningConfig_(provisioning), link_(link), board_(board),
        storage_(storage), clock_(clock), logger_(logger), entropy_(entropy),
        configurationStorage_(storage_, kMaximumConfigurationRecords),
        configurationInstaller_(configurationStorage_, kMaximumConfigurationRecords),
        hardware_(hardware), device_(link_, storage_, clock_, logger_),
        scheduleRuntime_(storage_, clock_),
        scheduleCompiler_(scheduleRuntime_.workspace()) {
    hardware_.attach(device_);
    scheduleRuntime_.handlers(applyScheduledWrite, requestScheduleRenewal,
                              reportScheduleStatus, this);
  }

  bool begin() {
    if (lifecycle_ != FlovaLifecycle::Idle) return false;
    prepareProvisioningIdentity();
    if (!board_.beginStorage()) {
      writeError("storage_begin_failed");
      logger_.log("[flova] lifecycle failed reason=storage_begin_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    scheduleRuntime_.begin();
    if (!board_.begin(handleProvisioning, this)) {
      logger_.log("[flova] lifecycle failed reason=board_begin_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }

    memset(&configurationImageWorkspace_, 0,
           sizeof(configurationImageWorkspace_));
    const bool hasConfiguration =
        storage_.read("config", &configurationImageWorkspace_,
                      sizeof(configurationImageWorkspace_)) &&
        flova::verifyConfigurationImage(configurationImageWorkspace_);
    memset(&pending_, 0, sizeof(pending_));
    const bool hasPending =
        storage_.read("prov_pending", &pending_, sizeof(pending_)) &&
        flova::verifyProvisioningImage(pending_);

    logger_.log(hasConfiguration
                    ? "[flova] stored configuration accepted"
                    : "[flova] stored configuration absent_or_invalid");

    if (hasPending && provisioningConfig_.enabled) {
      if (pending_.inProgress ||
          pending_.attempts >= kMaximumBootstrapAttempts) {
        storage_.remove("prov_pending");
        writeError("firmware_reset_during_bootstrap");
        return beginSetup();
      }
      if (!markBootstrapAttempt()) {
        writeError("storage_failed");
        storage_.remove("prov_pending");
        return beginSetup();
      }
      lifecycle_ = FlovaLifecycle::WaitingForWifi;
      bootstrapStartedAt_ = millis();
      if (!board_.beginStation(pending_.handoff.wifiSsid,
                               pending_.handoff.wifiPassword)) {
        failBootstrap("wifi_start_failed");
      }
      return true;
    }

    if (hasConfiguration) {
      runtimeConfiguration_ = configurationImageWorkspace_.configuration;
      if (!restoreActiveConfiguration()) {
        writeError("configuration_restore_failed");
        lifecycle_ = FlovaLifecycle::Failed;
        return false;
      }
      return beginSavedRuntime();
    }

    if (validStaticConfiguration()) return beginStaticRuntime();
    if (provisioningConfig_.enabled) return beginSetup();
    lifecycle_ = FlovaLifecycle::Failed;
    return false;
  }

  void run() {
    board_.loop();
    if (lifecycle_ == FlovaLifecycle::Setup || lifecycle_ == FlovaLifecycle::Restarting ||
        lifecycle_ == FlovaLifecycle::Failed || lifecycle_ == FlovaLifecycle::Idle) return;

    if (lifecycle_ == FlovaLifecycle::WaitingForWifi) {
      if (pending_.handoff.token[0]) {
        if (millis() - bootstrapStartedAt_ >= kBootstrapTimeoutMs) {
          failBootstrap(board_.stationConnected() ? "clock_sync_failed" : "wifi_timeout");
          return;
        }
        if (!board_.stationConnected() || !board_.clockReady()) return;
        if (!link_.beginBootstrap(pending_.handoff.linkUrl, pending_.handoff.token,
                                  provisioningConfig_.hardwareId,
                                  provisioningConfig_.firmwareTarget,
                                  pending_.handoff.linkSecret)) {
          failBootstrap("bootstrap_start_failed");
          return;
        }
        lifecycle_ = FlovaLifecycle::Bootstrapping;
        bootstrapStartedAt_ = millis();
        return;
      }
      if (!board_.stationConnected() || !board_.clockReady()) return;
      beginDeviceRuntime();
      return;
    }

    if (lifecycle_ == FlovaLifecycle::Bootstrapping) {
      link_.pollBootstrap();
      drainConfiguration(true);
      FlovaLinkBootstrapCommitted committed = {};
      if (link_.takeBootstrapCommitted(committed)) {
        if (!configurationCommitted_ ||
            committed.generation != activeConfigurationGeneration_) {
          failBootstrap("configuration_not_committed");
          return;
        }
        completeBootstrap(committed);
        return;
      }
      char error[flova::kProvisioningErrorBytes] = {};
      if (link_.takeBootstrapError(error, sizeof(error))) {
        failBootstrap(error[0] ? error : "bootstrap_rejected");
        return;
      }
      if (millis() - bootstrapStartedAt_ >= kBootstrapTimeoutMs) {
        failBootstrap("bootstrap_timeout");
      }
      return;
    }

    if (lifecycle_ == FlovaLifecycle::Runtime) {
      hardware_.setConnected(link_.connected());
      hardware_.run();
      device_.run();
      scheduleRuntime_.run();
      drainConfiguration(false);
      processOta();
      reportRuntimeStatus();
    }
  }

  bool startProvisioning() {
    if (!provisioningConfig_.enabled) return false;
    link_.disconnect();
    if (!board_.startProvisioning()) {
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    storage_.remove("config");
    storage_.remove("prov_pending");
    lifecycle_ = FlovaLifecycle::Setup;
    return true;
  }

  bool provisioning() const { return lifecycle_ == FlovaLifecycle::Setup || pending_.handoff.token[0]; }
  FlovaLifecycle lifecycle() const { return lifecycle_; }
  bool connected() const { return link_.connected(); }
  bool ready() const { return lifecycle_ == FlovaLifecycle::Runtime && device_.ready(); }
  const flova::Diagnostics& diagnostics() const { return device_.diagnostics(); }
  flova::Device& device() { return device_; }

  template <typename T>
  flova::Datastream<T> datastream(const char* key) { return device_.datastream<T>(key); }

 private:
  static const uint8_t kMaximumBootstrapAttempts = 3;
  static const uint32_t kBootstrapTimeoutMs = 30000UL;
  static const uint32_t kMaximumConfigurationRecords =
      FLOVA_DATASTREAM_CAPACITY + FLOVA_SCHEDULE_CAPACITY + 8;

  static FlovaProvisioningResponse handleProvisioning(void* context, const char* body,
                                                       size_t length) {
    if (!context) return FlovaProvisioningResponse::Invalid;
    return static_cast<FlovaClient*>(context)->acceptProvisioning(body, length);
  }

  FlovaProvisioningResponse acceptProvisioning(const char* body, size_t length) {
    memset(&pending_, 0, sizeof(pending_));
    if (!flova::parseProvisioningHandoff(body, length, pending_.handoff) ||
        !flova::generateSecret(pending_.handoff.linkSecret, entropy_)) {
      return FlovaProvisioningResponse::Invalid;
    }
    flova::makeProvisioningImage(pending_.handoff, pending_);
    if (!storage_.write("prov_pending", &pending_, sizeof(pending_))) {
      writeError("storage_failed");
      return FlovaProvisioningResponse::StorageFailed;
    }
    memset(&pending_, 0, sizeof(pending_));
    if (!storage_.read("prov_pending", &pending_, sizeof(pending_)) ||
        !flova::verifyProvisioningImage(pending_)) {
      writeError("storage_verify_failed");
      return FlovaProvisioningResponse::StorageFailed;
    }
    storage_.remove("prov_error");
    return FlovaProvisioningResponse::Accepted;
  }

  bool beginSetup() {
    lifecycle_ = board_.startProvisioning() ? FlovaLifecycle::Setup : FlovaLifecycle::Failed;
    logger_.log(lifecycle_ == FlovaLifecycle::Setup
                    ? "[flova] lifecycle setup_ap"
                    : "[flova] lifecycle failed reason=setup_ap_failed");
    return lifecycle_ == FlovaLifecycle::Setup;
  }

  bool beginSavedRuntime() {
    if (!link_.configure(runtimeConfiguration_.linkUrl,
                         runtimeConfiguration_.deviceId,
                         runtimeConfiguration_.linkSecret)) {
      writeError("runtime_link_configuration_failed");
      logger_.log("[flova] lifecycle failed reason=runtime_link_configuration_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    if (!board_.beginStation(runtimeConfiguration_.wifiSsid,
                             runtimeConfiguration_.wifiPassword)) {
      writeError("runtime_wifi_start_failed");
      logger_.log("[flova] lifecycle failed reason=runtime_wifi_start_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    logger_.log("[flova] lifecycle waiting_for_wifi");
    lifecycle_ = FlovaLifecycle::WaitingForWifi;
    return true;
  }

  bool beginStaticRuntime() {
    if (!link_.configure(config_.linkUrl, config_.deviceId, config_.secret)) {
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    if (provisioningConfig_.wifiSsid && provisioningConfig_.wifiSsid[0]) {
      if (!board_.beginStation(provisioningConfig_.wifiSsid,
                               provisioningConfig_.wifiPassword)) {
        lifecycle_ = FlovaLifecycle::Failed;
        return false;
      }
      lifecycle_ = FlovaLifecycle::WaitingForWifi;
      return true;
    }
    beginDeviceRuntime();
    return true;
  }

  void beginDeviceRuntime() {
    if (!device_.begin()) {
      writeError("runtime_device_begin_failed");
      logger_.log("[flova] lifecycle failed reason=runtime_device_begin_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return;
    }
    logger_.log("[flova] lifecycle runtime");
    lifecycle_ = FlovaLifecycle::Runtime;
  }

  bool markBootstrapAttempt() {
    flova::markProvisioningAttempt(pending_);
    return storage_.write("prov_pending", &pending_, sizeof(pending_));
  }

  flova::config::Ack applyConfiguration(
      const FlovaLinkConfigurationRecord& input) {
    if (input.phase == FlovaLinkConfigurationPhase::Begin) {
      flova::config::Begin begin;
      begin.messageId = input.messageId;
      begin.generation = input.generation;
      begin.schemaVersion = input.schemaVersion;
      begin.maximumRecordBytes = input.maximumRecordBytes;
      begin.recordCount = input.recordCount;
      memcpy(begin.checksum.bytes, input.checksum, sizeof(input.checksum));
      return configurationInstaller_.begin(begin);
    }
    if (input.phase == FlovaLinkConfigurationPhase::Record) {
      flova::config::Record& record = configurationInstaller_.workspace();
      record = flova::config::Record();
      record.messageId = input.messageId;
      record.generation = input.generation;
      record.sequence = input.sequence;
      record.kind = static_cast<flova::config::RecordKind>(input.recordType);
      record.length = input.recordLength;
      if (record.length > sizeof(record.body)) {
        flova::config::Ack rejected;
        rejected.messageId = input.messageId;
        rejected.generation = input.generation;
        rejected.sequence = input.sequence;
        rejected.status = flova::config::Status::InvalidRecord;
        return rejected;
      }
      memcpy(record.body, input.record, record.length);
      return configurationInstaller_.record(record);
    }
    flova::config::End end;
    end.messageId = input.messageId;
    end.generation = input.generation;
    end.recordCount = input.recordCount;
    memcpy(end.checksum.bytes, input.checksum, sizeof(input.checksum));
    return configurationInstaller_.end(end);
  }

  void drainConfiguration(bool bootstrapping) {
    configurationDecodeWorkspace_ = FlovaLinkConfigurationRecord();
    if (!link_.takeConfigurationRecord(configurationDecodeWorkspace_)) return;
    const flova::config::Ack ack =
        applyConfiguration(configurationDecodeWorkspace_);
    configurationReportWorkspace_ = FlovaLinkConfigurationReport();
    configurationReportWorkspace_.messageId = ack.messageId;
    configurationReportWorkspace_.generation = ack.generation;
    configurationReportWorkspace_.sequence = ack.sequence;
    memcpy(configurationReportWorkspace_.checksum,
           configurationDecodeWorkspace_.checksum,
           sizeof(configurationReportWorkspace_.checksum));
    configurationReportWorkspace_.status =
        ack.accepted() ? FlovaLinkResultStatus::Ok
                       : FlovaLinkResultStatus::Error;
    if (!ack.accepted()) {
      strncpy(configurationReportWorkspace_.errorCode,
              "configuration_rejected",
              sizeof(configurationReportWorkspace_.errorCode) - 1);
    }
    link_.publishConfigurationReport(configurationReportWorkspace_);
    if (!ack.accepted() ||
        configurationDecodeWorkspace_.phase != FlovaLinkConfigurationPhase::End)
      return;
    configurationCommitted_ = true;
    activeConfigurationGeneration_ = configurationDecodeWorkspace_.generation;
    memcpy(activeConfigurationChecksum_, configurationDecodeWorkspace_.checksum,
           sizeof(activeConfigurationChecksum_));
    link_.setConfigurationGeneration(activeConfigurationGeneration_);
    if (!bootstrapping) {
      link_.disconnect();
      lifecycle_ = FlovaLifecycle::Restarting;
      board_.scheduleRestart();
    }
  }

  bool restoreActiveConfiguration() {
    uint32_t newest = 0;
    uint32_t previous = 0;
    configurationStorage_.generations(newest, previous);
    const uint32_t candidates[2] = {newest, previous};
    for (size_t candidate = 0; candidate < 2; ++candidate) {
      const uint32_t generation = candidates[candidate];
      if (!generation) continue;
      flova::config::GenerationManifest manifest;
      if (!configurationStorage_.generationManifest(generation, manifest) ||
          !manifest.finalized ||
          manifest.recordCount > kMaximumConfigurationRecords) {
        configurationStorage_.discardGeneration(generation);
        continue;
      }
      configurationDigest_.reset();
      bool valid = true;
      for (uint32_t sequence = 0; sequence < manifest.recordCount; ++sequence) {
        if (!configurationInstaller_.loadWorkspace(generation, sequence)) {
          valid = false;
          break;
        }
        configurationDigest_.addRecord(configurationInstaller_.workspace());
      }
      flova::config::Checksum checksum;
      if (valid) configurationDigest_.finish(checksum);
      if (!valid || !checksum.equals(manifest.checksum)) {
        configurationStorage_.discardGeneration(generation);
        continue;
      }
      const bool scheduleAlreadyInstalled =
          scheduleRuntime_.revision() == generation;
      if (!prepareScheduleCompiler(generation, manifest.recordCount)) {
        configurationStorage_.discardGeneration(generation);
        continue;
      }
      for (uint32_t sequence = 0; sequence < manifest.recordCount; ++sequence) {
        if (!configurationInstaller_.loadWorkspace(generation, sequence))
          return false;
        const flova::config::Record& stored = configurationInstaller_.workspace();
        configurationDecodeWorkspace_ = FlovaLinkConfigurationRecord();
        if (!link_.decodeStoredConfigurationRecord(
                stored.body, stored.length, configurationDecodeWorkspace_) ||
            !configurationDecodeWorkspace_.hasTypedUnit ||
            !device_.applyConfigurationUnit(
                configurationDecodeWorkspace_.typedUnit) ||
            !hardware_.apply(configurationDecodeWorkspace_.typedUnit) ||
            !applyScheduleUnit(configurationDecodeWorkspace_.typedUnit)) {
          valid = false;
          break;
        }
      }
      if (!valid) {
        configurationStorage_.discardGeneration(generation);
        continue;
      }
      if (compilingSchedules_) {
        if (!scheduleCompiler_.finish() ||
            (!scheduleAlreadyInstalled && !scheduleRuntime_.installPrepared())) {
          configurationStorage_.discardGeneration(generation);
          continue;
        }
      } else {
        scheduleRuntime_.clear();
      }
      activeConfigurationGeneration_ = generation;
      memcpy(activeConfigurationChecksum_, manifest.checksum.bytes,
             sizeof(activeConfigurationChecksum_));
      configurationCommitted_ = true;
      link_.setConfigurationGeneration(generation);
      return true;
    }
    return true;
  }

  bool prepareScheduleCompiler(uint32_t generation, uint32_t recordCount) {
    uint8_t scheduleCount = 0;
    uint64_t generatedAt = 0;
    uint64_t validUntil = 0;
    for (uint32_t sequence = 0; sequence < recordCount; ++sequence) {
      if (!configurationInstaller_.loadWorkspace(generation, sequence))
        return false;
      const flova::config::Record& stored = configurationInstaller_.workspace();
      configurationDecodeWorkspace_ = FlovaLinkConfigurationRecord();
      if (!link_.decodeStoredConfigurationRecord(
              stored.body, stored.length, configurationDecodeWorkspace_) ||
          !configurationDecodeWorkspace_.hasTypedUnit) {
        return false;
      }
      const flova::config::Unit& unit =
          configurationDecodeWorkspace_.typedUnit;
      if (unit.kind != flova::config::UnitKind::Schedule) continue;
      if (scheduleCount == FLOVA_SCHEDULE_CAPACITY ||
          !unit.data.schedule.validUntil) {
        return false;
      }
      ++scheduleCount;
      if (!generatedAt || unit.data.schedule.validFrom < generatedAt)
        generatedAt = unit.data.schedule.validFrom;
      if (!validUntil || unit.data.schedule.validUntil < validUntil)
        validUntil = unit.data.schedule.validUntil;
    }
    compilingSchedules_ = scheduleCount != 0;
    if (!compilingSchedules_) return true;
    static const uint64_t kRenewBeforeMs =
        14ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
    const uint64_t renewBefore = validUntil > kRenewBeforeMs
                                     ? validUntil - kRenewBeforeMs
                                     : validUntil;
    return scheduleCompiler_.begin(generation, generatedAt, validUntil,
                                   renewBefore, scheduleCount);
  }

  bool applyScheduleUnit(const flova::config::Unit& unit) {
    if (!compilingSchedules_) {
      return unit.kind != flova::config::UnitKind::Schedule &&
             unit.kind != flova::config::UnitKind::ScheduleOccurrences;
    }
    if (unit.kind == flova::config::UnitKind::Schedule)
      return scheduleCompiler_.addSchedule(unit.data.schedule);
    if (unit.kind == flova::config::UnitKind::ScheduleOccurrences)
      return scheduleCompiler_.addOccurrences(unit.data.occurrences);
    return true;
  }

  static flova::WriteResult applyScheduledWrite(
      void* context, const char*, const flova::ScheduleAction& action,
      uint64_t) {
    FlovaClient* client = static_cast<FlovaClient*>(context);
    return client->device_.write(action.datastreamId, action.value,
                                 flova::Origin::Internal);
  }

  static void requestScheduleRenewal(void* context, uint32_t revision,
                                     uint64_t validUntil) {
    FlovaClient* client = static_cast<FlovaClient*>(context);
    FlovaLinkScheduleStatus status = {};
    status.messageId = client->nextControlMessageId();
    status.generation = client->activeConfigurationGeneration_;
    status.revision = revision;
    status.validUntilUtcMs = validUntil;
    status.status = FlovaLinkResultStatus::Ok;
    client->link_.publishScheduleRenew(status);
  }

  static void reportScheduleStatus(void* context, const char* code,
                                   uint32_t revision, uint64_t validUntil) {
    FlovaClient* client = static_cast<FlovaClient*>(context);
    FlovaLinkScheduleStatus status = {};
    status.messageId = client->nextControlMessageId();
    status.generation = client->activeConfigurationGeneration_;
    status.revision = revision;
    status.validUntilUtcMs = validUntil;
    status.status = strcmp(code, "installed") == 0
                        ? FlovaLinkResultStatus::Ok
                        : FlovaLinkResultStatus::Error;
    strncpy(status.errorCode, code ? code : "schedule_failed",
            sizeof(status.errorCode) - 1);
    client->link_.publishScheduleStatus(status);
  }

  void processOta() {
    if (otaResultPending_ && link_.connected()) {
      if (link_.publishOtaReport(otaResult_)) otaResultPending_ = false;
      return;
    }
    FlovaLinkOtaOffer offer = {};
    if (!link_.takeOtaOffer(offer)) return;
    otaResult_ = FlovaLinkOtaReport();
    otaResult_.messageId = nextControlMessageId();
    otaResult_.installId = offer.installId;
    if (!offer.installId.present || !offer.sizeBytes ||
        strncmp(offer.url, "https://", 8) != 0 ||
        (offer.firmwareTarget[0] && provisioningConfig_.firmwareTarget &&
         strcmp(offer.firmwareTarget, provisioningConfig_.firmwareTarget) != 0)) {
      otaResult_.status = FlovaLinkResultStatus::Error;
      strncpy(otaResult_.errorCode, "ota_offer_invalid",
              sizeof(otaResult_.errorCode) - 1);
      otaResultPending_ = true;
      return;
    }
    FlovaLinkOtaReport accepted = {};
    accepted.messageId = nextControlMessageId();
    accepted.installId = offer.installId;
    accepted.status = FlovaLinkResultStatus::Ok;
    if (!link_.publishOtaReport(accepted)) return;
    link_.disconnect();
    const flova::OtaInstallResult result = link_.installOta(offer);
    if (result == flova::OtaInstallResult::Installed) {
      lifecycle_ = FlovaLifecycle::Restarting;
      board_.scheduleRestart();
      return;
    }
    otaResult_.status = FlovaLinkResultStatus::Error;
    const char* error = result == flova::OtaInstallResult::HashMismatch
                            ? "ota_hash_mismatch"
                            : result == flova::OtaInstallResult::FlashFailed
                                  ? "ota_flash_failed"
                                  : result == flova::OtaInstallResult::ResourceUnavailable
                                        ? "resource_unavailable"
                                        : "ota_download_failed";
    strncpy(otaResult_.errorCode, error, sizeof(otaResult_.errorCode) - 1);
    otaResultPending_ = true;
  }

  void reportRuntimeStatus() {
    const bool connected = link_.connected();
    if (!connected) {
      runtimeReported_ = false;
      return;
    }
    const uint32_t now = millis();
    if (!runtimeReported_) {
      if (activeConfigurationGeneration_) {
        FlovaLinkConfigurationState state = {};
        state.messageId = nextControlMessageId();
        state.generation = activeConfigurationGeneration_;
        memcpy(state.checksum, activeConfigurationChecksum_,
               sizeof(state.checksum));
        state.status = FlovaLinkResultStatus::Ok;
        if (!link_.publishConfigurationState(state)) return;
      }
      runtimeReported_ = true;
      lastHeartbeatAt_ = 0;
    }
    if (lastHeartbeatAt_ && now - lastHeartbeatAt_ < 30000UL) return;
    FlovaLinkHeartbeat heartbeat = {};
    heartbeat.messageId = nextControlMessageId();
    heartbeat.configurationGeneration = activeConfigurationGeneration_
                                            ? activeConfigurationGeneration_
                                            : link_.configurationGeneration();
    heartbeat.uptimeMs = now;
    heartbeat.protocolVersion = 1;
    heartbeat.datastreamSlots = FLOVA_DATASTREAM_CAPACITY;
    heartbeat.scheduleSlots = FLOVA_SCHEDULE_RUNTIME_ENABLED
                                  ? FLOVA_SCHEDULE_CAPACITY
                                  : 0;
    heartbeat.otaCapable = true;
    strncpy(heartbeat.firmwareVersion, FLOVA_FIRMWARE_VERSION,
            sizeof(heartbeat.firmwareVersion) - 1);
    if (provisioningConfig_.firmwareTarget) {
      strncpy(heartbeat.firmwareTarget,
              provisioningConfig_.firmwareTarget,
              sizeof(heartbeat.firmwareTarget) - 1);
    }
    if (link_.publishHeartbeat(heartbeat)) lastHeartbeatAt_ = now ? now : 1;
  }

  uint64_t nextControlMessageId() {
    return device_.originateMessageId();
  }

  void failBootstrap(const char* error) {
    flova::markProvisioningFailure(pending_, error);
    writeError(error);
    link_.disconnect();
    if (pending_.attempts >= kMaximumBootstrapAttempts) {
      storage_.remove("prov_pending");
      beginSetup();
      return;
    }
    storage_.write("prov_pending", &pending_, sizeof(pending_));
    lifecycle_ = FlovaLifecycle::Restarting;
    board_.scheduleRestart();
  }

  void completeBootstrap(const FlovaLinkBootstrapCommitted& committed) {
    memset(&runtimeConfiguration_, 0, sizeof(runtimeConfiguration_));
    copyId(runtimeConfiguration_.deviceId,
           sizeof(runtimeConfiguration_.deviceId), committed.deviceId);
    flova::copyBounded(pending_.handoff.wifiSsid,
                       runtimeConfiguration_.wifiSsid, true);
    flova::copyBounded(pending_.handoff.wifiPassword,
                       runtimeConfiguration_.wifiPassword);
    flova::copyBounded(pending_.handoff.linkUrl,
                       runtimeConfiguration_.linkUrl, true);
    flova::copyBounded(pending_.handoff.linkSecret,
                       runtimeConfiguration_.linkSecret, true);
    runtimeConfiguration_.generation = committed.generation;
    if (!runtimeConfiguration_.deviceId[0]) {
      failBootstrap("bootstrap_device_id_missing");
      return;
    }
    flova::makeConfigurationImage(runtimeConfiguration_,
                                  configurationImageWorkspace_);
    if (!storage_.write("config", &configurationImageWorkspace_,
                        sizeof(configurationImageWorkspace_))) {
      failBootstrap("configuration_storage_failed");
      return;
    }
    memset(&configurationImageWorkspace_, 0,
           sizeof(configurationImageWorkspace_));
    if (!storage_.read("config", &configurationImageWorkspace_,
                       sizeof(configurationImageWorkspace_)) ||
        !flova::verifyConfigurationImage(configurationImageWorkspace_)) {
      failBootstrap("configuration_verify_failed");
      return;
    }
    storage_.remove("prov_pending");
    storage_.remove("prov_error");
    link_.disconnect();
    lifecycle_ = FlovaLifecycle::Restarting;
    board_.scheduleRestart();
  }

  bool validStaticConfiguration() const {
    return config_.deviceId && config_.deviceId[0] && config_.secret && config_.secret[0] &&
           config_.linkUrl && strncmp(config_.linkUrl, "wss://", 6) == 0;
  }

  static void copyId(char* output, size_t capacity, const FlovaLinkId& id) {
    if (!id.present) return;
    flova::formatUuidText(id.bytes, output, capacity);
  }

  void writeError(const char* error) {
    char safe[flova::kProvisioningErrorBytes] = {};
    flova::sanitizeProvisioningError(error, safe);
    storage_.write("prov_error", safe, sizeof(safe));
  }

  void prepareProvisioningIdentity() {
    if (!provisioningConfig_.hardwareId || !provisioningConfig_.hardwareId[0]) {
      if (board_.defaultHardwareId(hardwareIdWorkspace_, sizeof(hardwareIdWorkspace_)))
        provisioningConfig_.hardwareId = hardwareIdWorkspace_;
    }
    if (!provisioningConfig_.firmwareTarget || !provisioningConfig_.firmwareTarget[0]) {
      provisioningConfig_.firmwareTarget = board_.defaultFirmwareTarget();
    }
  }

  FlovaClientConfig config_;
  FlovaProvisioningConfig provisioningConfig_;
  FlovaClientLink& link_;
  FlovaBoardProvisioning& board_;
  flova::Storage& storage_;
  flova::Clock& clock_;
  flova::Logger& logger_;
  FlovaEntropySource& entropy_;
  FlovaLinkConfigurationStorage configurationStorage_;
  flova::config::Installer configurationInstaller_;
  flova::config::Digest configurationDigest_;
  flova::Hardware& hardware_;
  flova::Device device_;
  flova::ScheduleRuntime scheduleRuntime_;
  flova::ScheduleChunkCompiler scheduleCompiler_;
  FlovaLifecycle lifecycle_ = FlovaLifecycle::Idle;
  flova::DeviceConfiguration runtimeConfiguration_ = {};
  flova::ConfigurationImage configurationImageWorkspace_ = {};
  flova::ProvisioningHandoffImage pending_ = {};
  char hardwareIdWorkspace_[64] = {};
  uint32_t bootstrapStartedAt_ = 0;
  uint32_t activeConfigurationGeneration_ = 0;
  uint8_t activeConfigurationChecksum_[32] = {};
  bool configurationCommitted_ = false;
  FlovaLinkConfigurationRecord configurationDecodeWorkspace_ = {};
  FlovaLinkConfigurationReport configurationReportWorkspace_ = {};
  uint32_t lastHeartbeatAt_ = 0;
  bool runtimeReported_ = false;
  bool compilingSchedules_ = false;
  FlovaLinkOtaReport otaResult_ = {};
  bool otaResultPending_ = false;
};
