#pragma once

// Advanced Arduino composition entry point. Most applications should include
// FlovaEsp32.h, FlovaEsp8266.h, or the matching universal header instead. This
// header intentionally exposes FlovaClient and the bounded service seams for
// applications that supply their own Link or provisioning adapter.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <FlovaDevice.h>
#include <FlovaConfiguration.h>
#include <FlovaClientLink.h>
#include <FlovaProvisioningHandoff.h>
#include <FlovaWifiProvisioning.h>
#include <FlovaLinkConfigurationStorage.h>
#include <FlovaHardware.h>
#include <FlovaScheduleRuntime.h>
#include "adapters/ArduinoFlovaLink.h"
#include "adapters/ArduinoFlovaServices.h"
#include "adapters/ArduinoDeviceLink.h"
#include "adapters/ArduinoOtaInstaller.h"
#include "adapters/ArduinoFlovaHardware.h"
#include "adapters/ArduinoFlovaApplicationHardware.h"
#include "adapters/ArduinoFlovaUtcBootstrap.h"
#include "FlovaProvisioningAdapter.h"

#ifndef FLOVA_FIRMWARE_VERSION
#define FLOVA_FIRMWARE_VERSION "0.1.0"
#endif

enum class FlovaLifecycle : uint8_t {
  Idle,
  AwaitingProvisioning,
  Setup,
  WaitingForNetwork,
  Bootstrapping,
  RestartRequired,
  RestartScheduled,
  Runtime,
  Failed,
};

enum class FlovaRestartReason : uint8_t {
  None,
  ConfigurationActivation,
  OtaActivation,
  ResourceRecovery,
};

typedef void (*FlovaRestartHandler)(void* context, FlovaRestartReason reason);

// Internal Arduino orchestration. Board packages expose the public API.
class FlovaClient {
  struct ProvisioningConfig {
    const char* hardwareId;
    const char* firmwareTarget;
  };

 public:
  FlovaClient(FlovaClientLink& link, FlovaProvisioningAdapter& provisioning,
                       flova::Storage& storage, flova::Clock& clock,
                       flova::Logger& logger, FlovaEntropySource& entropy,
                       flova::Hardware& hardware)
      : provisioningConfig_{nullptr, nullptr},
        link_(link), provisioning_(provisioning),
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

  bool begin(bool allowProvisioning = false) {
    if (lifecycle_ != FlovaLifecycle::Idle) return false;
    managedProvisioning_ = allowProvisioning;
    pending_.lastError[0] = 0;
    if (!storage_.begin()) {
      setLastError("storage_begin_failed");
      logger_.log("[flova] lifecycle failed reason=storage_begin_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    prepareProvisioningIdentity();
    scheduleRuntime_.begin();
    if (!provisioning_.begin(handleProvisioning, this)) {
      writeError("board_begin_failed");
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

    if (hasPending && !hasConfiguration) {
      if (pending_.attempts >= kMaximumBootstrapAttempts) {
        if (!storage_.remove("prov_pending")) writeError("storage_failed");
        writeError("bootstrap_attempts_exhausted");
        return provisioningFallback();
      }
      // A reset can interrupt an otherwise valid bootstrap. The Engine binds
      // the handoff idempotently, so retry the verified pending generation.
      pending_.inProgress = 0;
      if (!markBootstrapAttempt()) {
        writeError("storage_failed");
        if (!storage_.remove("prov_pending")) writeError("storage_failed");
        return provisioningFallback();
      }
      lifecycle_ = FlovaLifecycle::WaitingForNetwork;
      bootstrapStartedAt_ = millis();
      if (!provisioning_.beginRuntime()) {
        failBootstrap("network_start_failed");
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

    return managedProvisioning_ ? beginSetup() : awaitProvisioning();
  }

  bool setFirmwareTarget(const char* target) {
    if (lifecycle_ != FlovaLifecycle::Idle || !target || !target[0] ||
        !copy(firmwareTargetWorkspace_, target)) {
      setLastError("invalid_firmware_target");
      return false;
    }
    provisioningConfig_.firmwareTarget = firmwareTargetWorkspace_;
    return true;
  }

  void run() {
    provisioning_.loop();
    if (lifecycle_ == FlovaLifecycle::Setup ||
        lifecycle_ == FlovaLifecycle::AwaitingProvisioning) {
      if (!handoffAccepted_) return;
      handoffAccepted_ = false;
      if (!markBootstrapAttempt()) {
        writeError("storage_failed");
        provisioningFallback();
        return;
      }
      lifecycle_ = FlovaLifecycle::WaitingForNetwork;
      bootstrapStartedAt_ = millis();
      if (!provisioning_.beginRuntime())
        failBootstrap("network_start_failed");
      return;
    }
    if (lifecycle_ == FlovaLifecycle::RestartRequired ||
        lifecycle_ == FlovaLifecycle::RestartScheduled ||
        lifecycle_ == FlovaLifecycle::Failed || lifecycle_ == FlovaLifecycle::Idle) return;

    if (lifecycle_ == FlovaLifecycle::WaitingForNetwork) {
      if (pending_.handoff.token[0]) {
        if (millis() - bootstrapStartedAt_ >= kBootstrapTimeoutMs) {
          failBootstrap(provisioning_.runtimeConnected() ? "clock_sync_failed" : "network_timeout");
          return;
        }
        if (!provisioning_.runtimeConnected() || !provisioning_.clockReady()) return;
        if (!link_.beginBootstrap(pending_.handoff.linkUrl, pending_.handoff.token,
                                  provisioningConfig_.hardwareId,
                                  provisioningConfig_.firmwareTarget,
                                  pending_.handoff.linkSecret)) {
          if (link_.resourceRecoveryRequired()) {
            flova::markProvisioningFailure(pending_, "resource_recovery");
            if (!storage_.write("prov_pending", &pending_, sizeof(pending_))) {
              writeError("storage_failed");
              provisioningFallback();
              return;
            }
            requestRestart(FlovaRestartReason::ResourceRecovery);
            return;
          }
          failBootstrap("bootstrap_start_failed");
          return;
        }
        lifecycle_ = FlovaLifecycle::Bootstrapping;
        bootstrapStartedAt_ = millis();
        return;
      }
      if (!provisioning_.runtimeConnected() || !provisioning_.clockReady()) return;
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
      if (link_.resourceRecoveryRequired()) {
        link_.disconnect();
        requestRestart(FlovaRestartReason::ResourceRecovery);
        return;
      }
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
    link_.disconnect();
    if (!storage_.remove("config") || !storage_.remove("prov_pending") ||
        !storage_.remove("prov_error")) {
      writeError("storage_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    memset(&runtimeConfiguration_, 0, sizeof(runtimeConfiguration_));
    memset(&pending_, 0, sizeof(pending_));
    if (managedProvisioning_) return beginSetup();
    awaitProvisioning();
    return true;
  }

  FlovaProvisioningResponse provision(const flova::ProvisioningHandoff& input) {
    if (lifecycle_ != FlovaLifecycle::Setup &&
        lifecycle_ != FlovaLifecycle::AwaitingProvisioning)
      return FlovaProvisioningResponse::Invalid;
    const FlovaProvisioningResponse result = acceptProvisioning(input);
    if (result == FlovaProvisioningResponse::Accepted) handoffAccepted_ = true;
    return result;
  }

  bool provisioning() const {
    return lifecycle_ == FlovaLifecycle::Setup ||
           lifecycle_ == FlovaLifecycle::AwaitingProvisioning ||
           pending_.handoff.token[0];
  }
  const char* lastError() const { return pending_.lastError; }
  FlovaLifecycle lifecycle() const { return lifecycle_; }
  bool connected() const { return link_.connected(); }
  bool ready() const { return lifecycle_ == FlovaLifecycle::Runtime && device_.ready(); }
  const flova::Diagnostics& diagnostics() const { return device_.diagnostics(); }
  flova::Device& device() { return device_; }
  void setRestartHandler(FlovaRestartHandler handler, void* context = nullptr) {
    restartHandler_ = handler;
    restartContext_ = context;
  }
  void setOtaEnabled(bool enabled) { otaEnabled_ = enabled; }
  bool restartRequired() const {
    return lifecycle_ == FlovaLifecycle::RestartRequired ||
           lifecycle_ == FlovaLifecycle::RestartScheduled;
  }
  FlovaRestartReason restartReason() const { return restartReason_; }

  template <typename T>
  flova::Datastream<T> datastream(const char* key) { return device_.datastream<T>(key); }

 private:
  static const uint8_t kMaximumBootstrapAttempts = 3;
  static const uint32_t kBootstrapTimeoutMs = 30000UL;
  static const uint32_t kMaximumConfigurationRecords =
      FLOVA_DATASTREAM_CAPACITY + FLOVA_SCHEDULE_CAPACITY + 8;

  static FlovaProvisioningResponse handleProvisioning(
      void* context, const flova::ProvisioningHandoff& input) {
    if (!context) return FlovaProvisioningResponse::Invalid;
    return static_cast<FlovaClient*>(context)->provision(input);
  }

  FlovaProvisioningResponse acceptProvisioning(
      const flova::ProvisioningHandoff& input) {
    memset(&pending_, 0, sizeof(pending_));
    if (!flova::copyBounded(input.linkUrl, pending_.handoff.linkUrl, true) ||
        !flova::copyBounded(input.token, pending_.handoff.token, true) ||
        !flova::generateSecret(pending_.handoff.linkSecret, entropy_)) {
      return FlovaProvisioningResponse::Invalid;
    }
    flova::makeProvisioningImage(pending_.handoff, pending_);
    if (!storage_.write("prov_pending", &pending_, sizeof(pending_))) {
      logger_.log("[flova] provisioning storage_failed stage=handoff_pending");
      writeError("storage_failed");
      return FlovaProvisioningResponse::StorageFailed;
    }
    memset(&pending_, 0, sizeof(pending_));
    if (!storage_.read("prov_pending", &pending_, sizeof(pending_)) ||
        !flova::verifyProvisioningImage(pending_)) {
      logger_.log("[flova] provisioning storage_failed stage=handoff_verify");
      writeError("storage_verify_failed");
      return FlovaProvisioningResponse::StorageFailed;
    }
    if (!storage_.remove("prov_error")) {
      logger_.log("[flova] provisioning storage_failed stage=clear_error");
      writeError("storage_failed");
      return FlovaProvisioningResponse::StorageFailed;
    }
    return FlovaProvisioningResponse::Accepted;
  }

  bool beginSetup() {
    if (!provisioning_.startProvisioning()) {
      writeError("provisioning_start_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    lifecycle_ = FlovaLifecycle::Setup;
    logger_.log("[flova] lifecycle setup_ap");
    return true;
  }

  bool awaitProvisioning() {
    lifecycle_ = FlovaLifecycle::AwaitingProvisioning;
    logger_.log("[flova] lifecycle awaiting_provisioning");
    return true;
  }

  bool provisioningFallback() {
    return managedProvisioning_ ? beginSetup() : awaitProvisioning();
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
    if (!provisioning_.beginRuntime()) {
      writeError("runtime_network_start_failed");
      logger_.log("[flova] lifecycle failed reason=runtime_network_start_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    logger_.log("[flova] lifecycle waiting_for_network");
    lifecycle_ = FlovaLifecycle::WaitingForNetwork;
    return true;
  }

  void beginDeviceRuntime() {
    if (!device_.begin()) {
      if (link_.resourceRecoveryRequired()) {
        requestRestart(FlovaRestartReason::ResourceRecovery);
        return;
      }
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
      if (!input.hasTypedUnit ||
          !device_.validateConfigurationUnit(input.typedUnit) ||
          !hardware_.validate(input.typedUnit)) {
        flova::config::Ack rejected;
        rejected.messageId = input.messageId;
        rejected.generation = input.generation;
        rejected.sequence = input.sequence;
        rejected.status = flova::config::Status::InvalidRecord;
        return rejected;
      }
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
    flova::config::Ack ack =
        applyConfiguration(configurationDecodeWorkspace_);
    if (ack.accepted() && ack.status == flova::config::Status::Accepted &&
        configurationDecodeWorkspace_.phase ==
            FlovaLinkConfigurationPhase::End) {
      flova::config::GenerationManifest manifest;
      if (!configurationStorage_.generationManifest(ack.generation, manifest) ||
          !validateGeneration(ack.generation, manifest)) {
        ack.status = configurationStorage_.discardGeneration(ack.generation)
                         ? flova::config::Status::VerificationFailure
                         : flova::config::Status::StorageFailure;
        configurationInstaller_.reset();
      } else if (!configurationInstaller_.promote(ack.generation)) {
        ack.status = flova::config::Status::StorageFailure;
      }
    }
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
      requestRestart(FlovaRestartReason::ConfigurationActivation);
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
        if (!configurationStorage_.discardGeneration(generation)) return false;
        continue;
      }
      if (!validateGeneration(generation, manifest)) {
        if (!configurationStorage_.discardGeneration(generation)) return false;
        continue;
      }
      if (!applyGeneration(generation, manifest.recordCount)) return false;
      activeConfigurationGeneration_ = generation;
      memcpy(activeConfigurationChecksum_, manifest.checksum.bytes,
             sizeof(activeConfigurationChecksum_));
      configurationCommitted_ = true;
      link_.setConfigurationGeneration(generation);
      return true;
    }
    return true;
  }

  bool decodeGenerationUnit(uint32_t generation, uint32_t sequence) {
    if (!configurationInstaller_.loadWorkspace(generation, sequence))
      return false;
    const flova::config::Record& stored = configurationInstaller_.workspace();
    configurationDecodeWorkspace_ = FlovaLinkConfigurationRecord();
    return link_.decodeStoredConfigurationRecord(
               stored.body, stored.length, configurationDecodeWorkspace_) &&
           configurationDecodeWorkspace_.hasTypedUnit;
  }

  bool hasValidatedDatastream(DatastreamId id) const {
    for (size_t i = 0; i < validationDatastreamCount_; ++i)
      if (validatedDatastreamId(i) == id) return true;
    return false;
  }

  struct ValidationSchedule {
    uint32_t id;
    uint16_t nextChunk;
    uint16_t chunkCount;
    uint16_t occurrences;
  };

  static const size_t kValidationDatastreamBytes =
      FLOVA_DATASTREAM_CAPACITY * sizeof(DatastreamId);
  static const size_t kValidationScheduleOffset = kValidationDatastreamBytes;

  uint8_t* validationWorkspace() {
    return reinterpret_cast<uint8_t*>(&configurationImageWorkspace_);
  }
  const uint8_t* validationWorkspace() const {
    return reinterpret_cast<const uint8_t*>(&configurationImageWorkspace_);
  }
  DatastreamId validatedDatastreamId(size_t index) const {
    DatastreamId id = FLOVA_INVALID_DATASTREAM_ID;
    memcpy(&id, validationWorkspace() + index * sizeof(id), sizeof(id));
    return id;
  }
  void setValidatedDatastreamId(size_t index, DatastreamId id) {
    memcpy(validationWorkspace() + index * sizeof(id), &id, sizeof(id));
  }
  ValidationSchedule validationSchedule(size_t index) const {
    ValidationSchedule value = {};
    memcpy(&value,
           validationWorkspace() + kValidationScheduleOffset +
               index * sizeof(value),
           sizeof(value));
    return value;
  }
  void setValidationSchedule(size_t index,
                             const ValidationSchedule& value) {
    memcpy(validationWorkspace() + kValidationScheduleOffset +
               index * sizeof(value),
           &value, sizeof(value));
  }

  bool uniqueDatastreamKey(uint32_t generation, uint32_t sequence,
                           const char* key) {
    char target[FLOVA_TEXT_CAPACITY] = {};
    if (!flova::copyBounded(key, target, true)) return false;
    for (uint32_t prior = 0; prior < sequence; ++prior) {
      if (!decodeGenerationUnit(generation, prior)) return false;
      const flova::config::Unit& unit = configurationDecodeWorkspace_.typedUnit;
      if (unit.kind == flova::config::UnitKind::Datastream &&
          strcmp(unit.data.datastream.key, target) == 0)
        return false;
    }
    return true;
  }

  bool validateGeneration(
      uint32_t generation,
      const flova::config::GenerationManifest& manifest) {
    configurationDigest_.reset();
    for (uint32_t sequence = 0; sequence < manifest.recordCount; ++sequence) {
      if (!configurationInstaller_.loadWorkspace(generation, sequence))
        return false;
      configurationDigest_.addRecord(configurationInstaller_.workspace());
    }
    flova::config::Checksum checksum;
    configurationDigest_.finish(checksum);
    if (!checksum.equals(manifest.checksum)) return false;

    validationDatastreamCount_ = 0;
    validationScheduleCount_ = 0;
    static_assert(kValidationScheduleOffset +
                          FLOVA_SCHEDULE_CAPACITY *
                              sizeof(ValidationSchedule) <=
                      sizeof(flova::ConfigurationImage),
                  "configuration validation workspace exceeds the shared image buffer");
    memset(&configurationImageWorkspace_, 0,
           sizeof(configurationImageWorkspace_));
    size_t mappingCount = 0;
    bool systemSeen = false;

    for (uint32_t sequence = 0; sequence < manifest.recordCount; ++sequence) {
      if (!decodeGenerationUnit(generation, sequence)) return false;
      const flova::config::Unit& unit = configurationDecodeWorkspace_.typedUnit;
      if (!device_.validateConfigurationUnit(unit) || !hardware_.validate(unit))
        return false;
      if (unit.kind == flova::config::UnitKind::Datastream) {
        if (validationDatastreamCount_ >= FLOVA_DATASTREAM_CAPACITY)
          return false;
        for (size_t i = 0; i < validationDatastreamCount_; ++i)
          if (validatedDatastreamId(i) == unit.data.datastream.id) return false;
        setValidatedDatastreamId(validationDatastreamCount_,
                                 unit.data.datastream.id);
        ++validationDatastreamCount_;
        if (unit.data.datastream.hasMapping &&
            ++mappingCount > FLOVA_HARDWARE_INPUT_CAPACITY +
                                 FLOVA_HARDWARE_OUTPUT_CAPACITY)
          return false;
        if (!uniqueDatastreamKey(generation, sequence,
                                 unit.data.datastream.key))
          return false;
      } else if (unit.kind == flova::config::UnitKind::System) {
        if (systemSeen) return false;
        systemSeen = true;
      } else if (unit.kind == flova::config::UnitKind::Schedule) {
        if (validationScheduleCount_ >= FLOVA_SCHEDULE_CAPACITY ||
            !unit.data.schedule.id || !unit.data.schedule.validUntil ||
            !unit.data.schedule.actionCount ||
            unit.data.schedule.actionCount >
                flova::config::kConfigurationScheduleActions)
          return false;
        for (size_t i = 0; i < validationScheduleCount_; ++i)
          if (validationSchedule(i).id == unit.data.schedule.id) return false;
        ValidationSchedule schedule = {};
        schedule.id = unit.data.schedule.id;
        setValidationSchedule(validationScheduleCount_++, schedule);
      } else if (unit.kind ==
                 flova::config::UnitKind::ScheduleOccurrences) {
        size_t index = validationScheduleCount_;
        for (size_t i = 0; i < validationScheduleCount_; ++i)
          if (validationSchedule(i).id == unit.data.occurrences.scheduleId) {
            index = i;
            break;
          }
        ValidationSchedule schedule =
            index < validationScheduleCount_
                ? validationSchedule(index)
                : ValidationSchedule();
        if (index == validationScheduleCount_ ||
            !unit.data.occurrences.chunkCount ||
            unit.data.occurrences.chunkIndex >=
                unit.data.occurrences.chunkCount ||
            unit.data.occurrences.chunkIndex !=
                schedule.nextChunk ||
            (schedule.chunkCount &&
             schedule.chunkCount !=
                 unit.data.occurrences.chunkCount) ||
            !unit.data.occurrences.occurrenceCount ||
            unit.data.occurrences.occurrenceCount >
                flova::config::kConfigurationOccurrenceChunk ||
            schedule.occurrences +
                    unit.data.occurrences.occurrenceCount >
                FLOVA_SCHEDULE_OCCURRENCE_CAPACITY)
          return false;
        schedule.chunkCount = unit.data.occurrences.chunkCount;
        ++schedule.nextChunk;
        schedule.occurrences += unit.data.occurrences.occurrenceCount;
        setValidationSchedule(index, schedule);
      }
    }

    for (size_t i = 0; i < validationScheduleCount_; ++i) {
      const ValidationSchedule schedule = validationSchedule(i);
      if (!schedule.chunkCount || schedule.nextChunk != schedule.chunkCount)
        return false;
    }

    for (uint32_t sequence = 0; sequence < manifest.recordCount; ++sequence) {
      if (!decodeGenerationUnit(generation, sequence)) return false;
      const flova::config::Unit& unit = configurationDecodeWorkspace_.typedUnit;
      if (unit.kind == flova::config::UnitKind::Safety &&
          !hasValidatedDatastream(unit.data.safety.datastreamId))
        return false;
      if (unit.kind == flova::config::UnitKind::Schedule)
        for (uint8_t action = 0; action < unit.data.schedule.actionCount;
             ++action)
          if (!hasValidatedDatastream(
                  unit.data.schedule.actions[action].datastreamId))
            return false;
    }
    return true;
  }

  bool applyGeneration(uint32_t generation, uint32_t recordCount) {
    const bool scheduleAlreadyInstalled =
        scheduleRuntime_.revision() == generation;
    if (!prepareScheduleCompiler(generation, recordCount)) return false;
    for (uint32_t sequence = 0; sequence < recordCount; ++sequence) {
      if (!decodeGenerationUnit(generation, sequence)) {
        hardware_.failSafe();
        return false;
      }
      const flova::config::Unit& unit = configurationDecodeWorkspace_.typedUnit;
      if (!device_.applyConfigurationUnit(unit) || !hardware_.apply(unit) ||
          !applyScheduleUnit(unit)) {
        hardware_.failSafe();
        return false;
      }
    }
    if (compilingSchedules_) {
      if (!scheduleCompiler_.finish() ||
          (!scheduleAlreadyInstalled && !scheduleRuntime_.installPrepared())) {
        hardware_.failSafe();
        return false;
      }
    } else {
      scheduleRuntime_.clear();
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
    if (!otaEnabled_) {
      otaResult_.status = FlovaLinkResultStatus::Error;
      strncpy(otaResult_.errorCode, "ota_not_enabled",
              sizeof(otaResult_.errorCode) - 1);
      otaResultPending_ = true;
      return;
    }
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
      requestRestart(FlovaRestartReason::OtaActivation);
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
    heartbeat.otaCapable = otaEnabled_;
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
      if (!storage_.remove("prov_pending")) writeError("storage_failed");
      managedProvisioning_ ? beginSetup() : awaitProvisioning();
      return;
    }
    if (!storage_.write("prov_pending", &pending_, sizeof(pending_)) ||
        !markBootstrapAttempt()) {
      writeError("storage_failed");
      managedProvisioning_ ? beginSetup() : awaitProvisioning();
      return;
    }
    lifecycle_ = FlovaLifecycle::WaitingForNetwork;
    bootstrapStartedAt_ = millis();
  }

  void completeBootstrap(const FlovaLinkBootstrapCommitted& committed) {
    memset(&runtimeConfiguration_, 0, sizeof(runtimeConfiguration_));
    copyId(runtimeConfiguration_.deviceId,
           sizeof(runtimeConfiguration_.deviceId), committed.deviceId);
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
    if (!storage_.remove("prov_pending") || !storage_.remove("prov_error")) {
      failBootstrap("configuration_cleanup_failed");
      return;
    }
    link_.disconnect();
    memset(&pending_, 0, sizeof(pending_));
    if (!restoreActiveConfiguration()) {
      writeError("configuration_restore_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return;
    }
    if (!link_.configure(runtimeConfiguration_.linkUrl,
                         runtimeConfiguration_.deviceId,
                         runtimeConfiguration_.linkSecret)) {
      writeError("runtime_link_configuration_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return;
    }
    beginDeviceRuntime();
  }

  void requestRestart(FlovaRestartReason reason) {
    restartReason_ = reason;
    if (!restartHandler_) {
      lifecycle_ = FlovaLifecycle::RestartRequired;
      return;
    }
    lifecycle_ = FlovaLifecycle::RestartScheduled;
    restartHandler_(restartContext_, reason);
  }

  static void copyId(char* output, size_t capacity, const FlovaLinkId& id) {
    if (!id.present) return;
    flova::formatUuidText(id.bytes, output, capacity);
  }

  template <size_t Capacity>
  static bool copy(char (&output)[Capacity], const char* input) {
    if (!input) input = "";
    const size_t length = strlen(input);
    if (length >= Capacity) {
      output[0] = 0;
      return false;
    }
    memcpy(output, input, length + 1);
    return true;
  }

  void writeError(const char* error) {
    setLastError(error);
    char message[96] = {};
    snprintf(message, sizeof(message), "[flova] error=%s",
             pending_.lastError[0] ? pending_.lastError : "unknown");
    logger_.log(message);
    storage_.write("prov_error", pending_.lastError,
                   sizeof(pending_.lastError));
  }

  void setLastError(const char* error) {
    flova::sanitizeProvisioningError(error, pending_.lastError);
  }

  void prepareProvisioningIdentity() {
    if (!provisioningConfig_.hardwareId || !provisioningConfig_.hardwareId[0]) {
      if (provisioning_.defaultHardwareId(hardwareIdWorkspace_, sizeof(hardwareIdWorkspace_)))
        provisioningConfig_.hardwareId = hardwareIdWorkspace_;
    }
    if (!provisioningConfig_.firmwareTarget || !provisioningConfig_.firmwareTarget[0]) {
      provisioningConfig_.firmwareTarget = provisioning_.defaultFirmwareTarget();
    }
  }

  ProvisioningConfig provisioningConfig_;
  FlovaClientLink& link_;
  FlovaProvisioningAdapter& provisioning_;
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
  char firmwareTargetWorkspace_[65] = {};
  uint32_t bootstrapStartedAt_ = 0;
  uint32_t activeConfigurationGeneration_ = 0;
  uint8_t activeConfigurationChecksum_[32] = {};
  bool configurationCommitted_ = false;
  FlovaLinkConfigurationRecord configurationDecodeWorkspace_ = {};
  FlovaLinkConfigurationReport configurationReportWorkspace_ = {};
  uint32_t lastHeartbeatAt_ = 0;
  bool runtimeReported_ = false;
  bool compilingSchedules_ = false;
  bool managedProvisioning_ = false;
  bool handoffAccepted_ = false;
  FlovaLinkOtaReport otaResult_ = {};
  bool otaResultPending_ = false;
  bool otaEnabled_ = false;
  size_t validationDatastreamCount_ = 0;
  size_t validationScheduleCount_ = 0;
  FlovaRestartHandler restartHandler_ = nullptr;
  void* restartContext_ = nullptr;
  FlovaRestartReason restartReason_ = FlovaRestartReason::None;
};
