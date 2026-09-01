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
#include <FlovaArduinoPlatform.h>
#include <FlovaProvisioningHandoff.h>
#include <FlovaWifiProvisioning.h>
#include <FlovaLinkConfigurationStorage.h>
#include <FlovaHardware.h>
#include <FlovaScheduleRuntime.h>
#include "adapters/ArduinoFlovaServices.h"
#include "FlovaProvisioningAdapter.h"
#include "FlovaRuntimeServices.h"

#ifndef FLOVA_FIRMWARE_VERSION
#define FLOVA_FIRMWARE_VERSION "0.1.0"
#endif

#include "FlovaFirmwareMetadata.h"

enum class FlovaLifecycle : uint8_t {
  Idle,
  AwaitingProvisioning,
  Setup,
  WaitingForNetwork,
  RestoringConfiguration,
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
  ProvisioningRecovery,
  FactoryReset,
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
              FlovaNetworkRuntime& network,
              FlovaTlsClockBootstrap& tlsClock,
              FlovaBoardIdentity& identity, flova::Storage& storage,
              flova::Clock& clock, flova::Logger& logger,
              FlovaEntropySource& entropy, flova::Hardware& hardware)
      : provisioningConfig_{nullptr, nullptr},
        link_(link), provisioning_(provisioning), network_(network),
        tlsClock_(tlsClock), identity_(identity),
        storage_(storage), clock_(clock), logger_(logger), entropy_(entropy),
        configurationStorage_(storage_, kMaximumConfigurationRecords),
        configurationInstaller_(configurationStorage_, kMaximumConfigurationRecords),
        hardware_(hardware), device_(link_, storage_, clock_, logger_),
        scheduleRuntime_(storage_, clock_),
        scheduleCompiler_(scheduleRuntime_.workspace()) {
    hardware_.attach(device_);
    hardware_.setFactoryResetHandler(handlePhysicalFactoryReset, this);
    device_.setFactoryResetHandler(handleFactoryResetCommand, this);
    link_.setHardwareCapabilities(hardware_.capabilities());
    scheduleRuntime_.handlers(applyScheduledWrite, requestScheduleRenewal,
                              reportScheduleStatus, this);
  }

  bool begin(bool allowProvisioning = false) {
#if defined(FLOVA_OTA_METADATA_ENABLED)
    flovaFirmwareMetadataRuntimePointer = flovaFirmwareMetadata;
#endif
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
    if (!validFirmwareTarget(provisioningConfig_.firmwareTarget)) {
      setLastError("invalid_firmware_target");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
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
    memset(&otaPendingRecord_, 0, sizeof(otaPendingRecord_));
    otaPendingRecordValid_ =
        storage_.read("ota_pending", &otaPendingRecord_, sizeof(otaPendingRecord_)) &&
        otaPendingRecord_.magic == kOtaPendingMagic &&
        otaPendingRecord_.installId.present && otaPendingRecord_.releaseId.present &&
        otaPendingRecord_.version[0];
    otaActivationFailurePending_ =
        otaPendingRecordValid_ && strcmp(otaPendingRecord_.version, FLOVA_FIRMWARE_VERSION) != 0;
    otaBootState_ = link_.otaBootState();
    otaRollbackReason_[0] = 0;
    if (otaActivationFailurePending_) {
      otaBootState_ = FlovaOtaBootState::RolledBack;
      copy(otaRollbackReason_, "ota_activation_failed");
    }

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
      if (!network_.begin()) {
        failBootstrap("network_start_failed");
      }
      return true;
    }

    if (hasConfiguration) {
      runtimeConfiguration_ = configurationImageWorkspace_.configuration;
      startConfigurationRestore(ConfigurationWorkMode::BootRestore);
      return true;
    }

    return managedProvisioning_ ? beginSetup() : awaitProvisioning();
  }

  bool setFirmwareTarget(const char* target) {
    if (lifecycle_ != FlovaLifecycle::Idle || !validFirmwareTarget(target) ||
        !copy(firmwareTargetWorkspace_, target)) {
      setLastError("invalid_firmware_target");
      return false;
    }
    provisioningConfig_.firmwareTarget = firmwareTargetWorkspace_;
    return true;
  }

  void run() {
    if (lifecycle_ == FlovaLifecycle::Setup ||
        lifecycle_ == FlovaLifecycle::AwaitingProvisioning)
      provisioning_.loop();
    network_.loop();
    tlsClock_.loop(network_.connected());
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
      if (!provisioning_.stopProvisioning())
        failBootstrap("provisioning_stop_failed");
      else if (!network_.begin())
        failBootstrap("network_start_failed");
      return;
    }
    if (lifecycle_ == FlovaLifecycle::RestartRequired ||
        lifecycle_ == FlovaLifecycle::RestartScheduled ||
        lifecycle_ == FlovaLifecycle::Failed || lifecycle_ == FlovaLifecycle::Idle) return;

    if (lifecycle_ == FlovaLifecycle::RestoringConfiguration) {
      stepConfigurationWork();
      return;
    }

    if (lifecycle_ == FlovaLifecycle::WaitingForNetwork) {
      if (pending_.handoff.token[0]) {
        if (millis() - bootstrapStartedAt_ >= kBootstrapTimeoutMs) {
          failBootstrap(network_.connected() ? "clock_sync_failed"
                                             : "network_timeout");
          return;
        }
        if (!network_.connected() || !tlsClock_.ready()) return;
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
      if (!network_.connected() || !tlsClock_.ready()) return;
      beginDeviceRuntime();
      return;
    }

    if (lifecycle_ == FlovaLifecycle::Bootstrapping) {
      link_.pollBootstrap();
      if (configurationWork_.mode != ConfigurationWorkMode::None) {
        stepConfigurationWork();
        return;
      }
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
      link_.setConnectionAllowed(network_.connected() && tlsClock_.ready());
      if (link_.resourceRecoveryRequired()) {
        link_.disconnect();
        requestRestart(FlovaRestartReason::ResourceRecovery);
        return;
      }
      hardware_.setConnected(link_.connected());
      hardware_.run();
      if (lifecycle_ != FlovaLifecycle::Runtime) return;
      device_.run();
      if (factoryResetRequestedAt_ &&
          (!device_.commandResultPending(factoryResetCommandId_) ||
           millis() - factoryResetRequestedAt_ >= kFactoryResetAckGraceMs)) {
        factoryReset();
        return;
      }
      scheduleRuntime_.run();
      if (configurationWork_.mode != ConfigurationWorkMode::None) {
        stepConfigurationWork();
        return;
      }
      drainConfiguration(false);
      processOta();
      serviceOtaBoot();
      reportRuntimeStatus();
    }
  }

  bool startProvisioning() {
    link_.disconnect();
    if (!storage_.remove("config") || !storage_.remove("prov_pending") ||
        !storage_.remove("prov_error") || !storage_.remove("ota_pending")) {
      writeError("storage_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    otaPendingRecordValid_ = false;
    otaActivationFailurePending_ = false;
    memset(&runtimeConfiguration_, 0, sizeof(runtimeConfiguration_));
    memset(&pending_, 0, sizeof(pending_));
    configurationVerifiedGeneration_ = 0;
    if (managedProvisioning_) return provisioningFallback();
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
  bool runtimeReady() const { return lifecycle_ == FlovaLifecycle::Runtime; }
  bool ready() const { return lifecycle_ == FlovaLifecycle::Runtime && device_.ready(); }
  const flova::Diagnostics& diagnostics() const { return device_.diagnostics(); }
  flova::Device& device() { return device_; }
  void setRestartHandler(FlovaRestartHandler handler, void* context = nullptr) {
    restartHandler_ = handler;
    restartContext_ = context;
  }
  void setOtaEnabled(bool enabled) { otaEnabled_ = enabled; }
  void setOtaProfile(FlovaOtaStrategy strategy, const char* bootLayoutVersion,
                     bool rollbackCapable) {
    link_.setOtaProfile(strategy, bootLayoutVersion, rollbackCapable);
  }
  bool restartRequired() const {
    return lifecycle_ == FlovaLifecycle::RestartRequired ||
           lifecycle_ == FlovaLifecycle::RestartScheduled;
  }
  FlovaRestartReason restartReason() const { return restartReason_; }

  // Erases Flova runtime/provisioning state. Custom application storage and
  // application-owned network credentials remain outside this boundary.
  bool factoryReset() {
    if (!network_.stop() || !network_.clearCredentials()) return false;
    link_.disconnect();
    hardware_.failSafe();
    if (!storage_.clear()) return false;
    factoryResetRequestedAt_ = 0;
    factoryResetCommandId_[0] = 0;
    requestRestart(FlovaRestartReason::FactoryReset);
    return true;
  }

  template <typename T>
  flova::Datastream<T> datastream(const char* key) { return device_.datastream<T>(key); }

 private:
  static const uint8_t kMaximumBootstrapAttempts = 3;
  static const uint32_t kBootstrapTimeoutMs = 30000UL;
  static const uint32_t kOtaHealthWindowMs = 30000UL;
  static const uint32_t kOtaHealthDeadlineMs = 120000UL;
  static const uint32_t kFactoryResetAckGraceMs = 5000UL;
  static const uint32_t kMaximumConfigurationRecords =
      FLOVA_DATASTREAM_CAPACITY + FLOVA_SCHEDULE_CAPACITY + 8;
  static const uint32_t kOtaPendingMagic = 0x4f544131UL;

  enum class ConfigurationWorkMode : uint8_t {
    None,
    BootRestore,
    BootstrapRestore,
    BootstrapApply,
    TransferVerify,
  };

  enum class ConfigurationWorkPhase : uint8_t {
    Idle,
    SelectCandidate,
    Digest,
    Semantic,
    KeyScan,
    References,
    PrepareSchedules,
    ApplyInit,
    Apply,
    Finish,
  };

  struct ConfigurationWork {
    ConfigurationWorkMode mode = ConfigurationWorkMode::None;
    ConfigurationWorkPhase phase = ConfigurationWorkPhase::Idle;
    uint32_t candidates[2] = {};
    uint8_t candidateIndex = 0;
    uint32_t generation = 0;
    flova::config::GenerationManifest manifest = {};
    uint32_t sequence = 0;
    uint32_t priorSequence = 0;
    char keyTarget[FLOVA_TEXT_CAPACITY] = {};
    uint16_t mappedPins[FLOVA_HARDWARE_INPUT_CAPACITY +
                       FLOVA_HARDWARE_OUTPUT_CAPACITY] = {};
    size_t mappedPinCount = 0;
    size_t inputMappingCount = 0;
    size_t outputMappingCount = 0;
    uint16_t statusLedPin = UINT16_MAX;
    bool systemSeen = false;
    bool scheduleAlreadyInstalled = false;
    uint8_t scheduleCount = 0;
    uint64_t generatedAt = 0;
    uint64_t validUntil = 0;
    bool bootstrapping = false;
  };

  static FlovaProvisioningResponse handleProvisioning(
      void* context, const flova::ProvisioningHandoff& input) {
    if (!context) return FlovaProvisioningResponse::Invalid;
    return static_cast<FlovaClient*>(context)->provision(input);
  }

  static flova::WriteResult handleFactoryResetCommand(void* context,
                                                       const char* commandId) {
    FlovaClient* self = static_cast<FlovaClient*>(context);
    if (!self || self->factoryResetRequestedAt_)
      return flova::WriteResult::noChange();
    if (!copy(self->factoryResetCommandId_, commandId))
      return flova::WriteResult::reject("factory_reset_invalid");
    const uint32_t now = millis();
    self->factoryResetRequestedAt_ = now ? now : 1;
    return flova::WriteResult::accept();
  }

  static void handlePhysicalFactoryReset(void* context) {
    FlovaClient* self = static_cast<FlovaClient*>(context);
    if (self) self->factoryReset();
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
    if (!network_.stop()) {
      writeError("network_stop_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
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
    if (!managedProvisioning_) return awaitProvisioning();
    if (provisioning_.requiresRestartBeforeProvisioning()) {
      requestRestart(FlovaRestartReason::ProvisioningRecovery);
      return true;
    }
    return beginSetup();
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
    if (!network_.begin()) {
      writeError("runtime_network_start_failed");
      logger_.log("[flova] lifecycle failed reason=runtime_network_start_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return false;
    }
    // Starting the application runtime is independent from cloud reachability.
    // ArduinoFlovaLink keeps connection attempts gated until Wi-Fi and the TLS
    // clock are ready, while Device restores local hardware immediately.
    beginDeviceRuntime();
    return lifecycle_ == FlovaLifecycle::Runtime;
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

  const char* configurationErrorCode(flova::config::Status status) const {
    if (status == flova::config::Status::InvalidRecord &&
        hardware_.configurationError()[0])
      return hardware_.configurationError();
    switch (status) {
      case flova::config::Status::InvalidBegin:
        return "configuration_invalid_begin";
      case flova::config::Status::InvalidRecord:
        return "configuration_invalid_record";
      case flova::config::Status::InvalidEnd:
        return "configuration_invalid_end";
      case flova::config::Status::TransferActive:
        return "configuration_transfer_active";
      case flova::config::Status::StaleGeneration:
        return "configuration_stale_generation";
      case flova::config::Status::OutOfOrder:
        return "configuration_out_of_order";
      case flova::config::Status::DuplicateMismatch:
        return "configuration_duplicate_mismatch";
      case flova::config::Status::ChecksumMismatch:
        return "configuration_checksum_mismatch";
      case flova::config::Status::StorageFailure:
        return "configuration_storage_failed";
      case flova::config::Status::VerificationFailure:
        return "configuration_verification_failed";
      case flova::config::Status::Accepted:
      case flova::config::Status::Duplicate:
      case flova::config::Status::AlreadyCommitted:
        return "";
    }
    return "configuration_rejected";
  }

  void drainConfiguration(bool bootstrapping) {
    if (configurationWork_.mode != ConfigurationWorkMode::None) return;
    configurationDecodeWorkspace_ = FlovaLinkConfigurationRecord();
    if (!link_.takeConfigurationRecord(configurationDecodeWorkspace_)) return;
    const FlovaLinkConfigurationPhase phase =
        configurationDecodeWorkspace_.phase;
    configurationReportWorkspace_ = FlovaLinkConfigurationReport();
    configurationReportWorkspace_.messageId =
        configurationDecodeWorkspace_.messageId;
    configurationReportWorkspace_.generation =
        configurationDecodeWorkspace_.generation;
    configurationReportWorkspace_.sequence =
        configurationDecodeWorkspace_.sequence;
    memcpy(configurationReportWorkspace_.checksum,
           configurationDecodeWorkspace_.checksum,
           sizeof(configurationReportWorkspace_.checksum));
    flova::config::Ack ack =
        applyConfiguration(configurationDecodeWorkspace_);
    // The CONFIG_END input has a record_count, not a sequence. The installer
    // returns the protocol acknowledgement sequence for each phase, so copy
    // all acknowledgement identity before verification can defer the report.
    configurationReportWorkspace_.messageId = ack.messageId;
    configurationReportWorkspace_.generation = ack.generation;
    configurationReportWorkspace_.sequence = ack.sequence;
    if (ack.accepted() && ack.status == flova::config::Status::Accepted &&
        phase == FlovaLinkConfigurationPhase::End) {
      flova::config::GenerationManifest manifest;
      if (configurationStorage_.generationManifest(ack.generation, manifest)) {
        beginConfigurationVerification(ack.generation, manifest, bootstrapping);
        return;
      }
      ack.status = flova::config::Status::StorageFailure;
    }
    publishConfigurationReport(ack.status, phase, bootstrapping);
    if (phase == FlovaLinkConfigurationPhase::End && ack.accepted() &&
        !bootstrapping) {
      link_.disconnect();
      requestRestart(FlovaRestartReason::ConfigurationActivation);
    }
  }

  void startConfigurationRestore(ConfigurationWorkMode mode,
                                 uint32_t generation = 0) {
    configurationWork_ = ConfigurationWork();
    configurationWork_.mode = mode;
    configurationWork_.scheduleAlreadyInstalled = false;
    configurationWork_.bootstrapping = mode == ConfigurationWorkMode::BootstrapRestore ||
                                        mode == ConfigurationWorkMode::BootstrapApply;
    if (mode == ConfigurationWorkMode::BootstrapRestore) {
      configurationWork_.candidates[0] = generation;
      configurationWork_.candidateIndex = 0;
      configurationWork_.phase = ConfigurationWorkPhase::SelectCandidate;
    } else if (mode == ConfigurationWorkMode::BootstrapApply) {
      configurationWork_.generation = generation;
      configurationWork_.scheduleAlreadyInstalled =
          scheduleRuntime_.revision() == generation;
      configurationWork_.phase = ConfigurationWorkPhase::PrepareSchedules;
      if (!configurationStorage_.generationManifest(
              generation, configurationWork_.manifest)) {
        failConfigurationWork(false);
        return;
      }
    } else {
      configurationStorage_.generations(configurationWork_.candidates[0],
                                        configurationWork_.candidates[1]);
      configurationWork_.phase = ConfigurationWorkPhase::SelectCandidate;
    }
    lifecycle_ = FlovaLifecycle::RestoringConfiguration;
  }

  void beginConfigurationVerification(
      uint32_t generation, const flova::config::GenerationManifest& manifest,
      bool bootstrapping) {
    configurationWork_ = ConfigurationWork();
    configurationWork_.mode = ConfigurationWorkMode::TransferVerify;
    configurationWork_.phase = ConfigurationWorkPhase::Digest;
    configurationWork_.generation = generation;
    configurationWork_.manifest = manifest;
    configurationWork_.bootstrapping = bootstrapping;
    configurationWork_.sequence = 0;
    configurationDigest_.reset();
    logger_.log("[flova] configuration verification started");
  }

  void stepConfigurationWork() {
    switch (configurationWork_.phase) {
      case ConfigurationWorkPhase::SelectCandidate:
        stepConfigurationCandidate();
        return;
      case ConfigurationWorkPhase::Digest:
        stepConfigurationDigest();
        return;
      case ConfigurationWorkPhase::Semantic:
        stepConfigurationSemantic();
        return;
      case ConfigurationWorkPhase::KeyScan:
        stepConfigurationKeyScan();
        return;
      case ConfigurationWorkPhase::References:
        stepConfigurationReferences();
        return;
      case ConfigurationWorkPhase::PrepareSchedules:
        stepConfigurationSchedules();
        return;
      case ConfigurationWorkPhase::ApplyInit:
        hardware_.resetConfiguration();
        configurationWork_.sequence = 0;
        configurationWork_.phase = ConfigurationWorkPhase::Apply;
        return;
      case ConfigurationWorkPhase::Apply:
        stepConfigurationApply();
        return;
      case ConfigurationWorkPhase::Finish:
        finishConfigurationApply();
        return;
      case ConfigurationWorkPhase::Idle:
        return;
    }
  }

  void stepConfigurationCandidate() {
    while (configurationWork_.candidateIndex < 2) {
      const uint32_t generation =
          configurationWork_.candidates[configurationWork_.candidateIndex++];
      if (!generation) continue;
      configurationWork_.generation = generation;
      if (!configurationStorage_.generationManifest(
              generation, configurationWork_.manifest) ||
          !configurationWork_.manifest.finalized ||
          configurationWork_.manifest.recordCount > kMaximumConfigurationRecords) {
        if (!configurationStorage_.discardGeneration(generation)) {
          failConfigurationWork(false);
          return;
        }
        return;
      }
      configurationWork_.scheduleAlreadyInstalled =
          scheduleRuntime_.revision() == generation;
      configurationWork_.sequence = 0;
      configurationDigest_.reset();
      configurationWork_.phase = ConfigurationWorkPhase::Digest;
      logger_.log("[flova] configuration restore candidate");
      return;
    }
    completeConfigurationRestore();
  }

  void stepConfigurationDigest() {
    if (configurationWork_.sequence < configurationWork_.manifest.recordCount) {
      if (!configurationInstaller_.loadWorkspace(
              configurationWork_.generation, configurationWork_.sequence)) {
        failConfigurationWork(true);
        return;
      }
      configurationDigest_.addRecord(configurationInstaller_.workspace());
      ++configurationWork_.sequence;
      return;
    }
    flova::config::Checksum checksum;
    configurationDigest_.finish(checksum);
    if (!checksum.equals(configurationWork_.manifest.checksum)) {
      failConfigurationWork(true);
      return;
    }
    configurationWork_.sequence = 0;
    configurationWork_.priorSequence = 0;
    validationDatastreamCount_ = 0;
    validationScheduleCount_ = 0;
    configurationWork_.mappedPinCount = 0;
    configurationWork_.inputMappingCount = 0;
    configurationWork_.outputMappingCount = 0;
    configurationWork_.statusLedPin = UINT16_MAX;
    configurationWork_.systemSeen = false;
    memset(&configurationImageWorkspace_, 0,
           sizeof(configurationImageWorkspace_));
    configurationWork_.phase = ConfigurationWorkPhase::Semantic;
  }

  bool validateConfigurationUnit(const flova::config::Unit& unit) {
    if (!device_.validateConfigurationUnit(unit) || !hardware_.validate(unit))
      return false;
    const flova::HardwareCapabilities capabilities = hardware_.capabilities();
    if (unit.kind == flova::config::UnitKind::Datastream) {
      if (validationDatastreamCount_ >= FLOVA_DATASTREAM_CAPACITY)
        return false;
      for (size_t i = 0; i < validationDatastreamCount_; ++i)
        if (validatedDatastreamId(i) == unit.data.datastream.id) return false;
      setValidatedDatastreamId(validationDatastreamCount_,
                               unit.data.datastream.id);
      ++validationDatastreamCount_;
      if (capabilities.automaticMapping && unit.data.datastream.hasMapping) {
        const flova::config::MappingKind kind =
            unit.data.datastream.mapping.kind;
        if (kind == flova::config::MappingKind::DigitalInput ||
            kind == flova::config::MappingKind::AnalogInput) {
          if (++configurationWork_.inputMappingCount > capabilities.inputSlots)
            return false;
        } else if (kind == flova::config::MappingKind::DigitalOutput ||
                   kind == flova::config::MappingKind::PwmOutput) {
          if (++configurationWork_.outputMappingCount > capabilities.outputSlots)
            return false;
        }
        const uint16_t pin = unit.data.datastream.mapping.pin;
        if ((configurationWork_.statusLedPin != UINT16_MAX &&
             configurationWork_.statusLedPin == pin) ||
            configurationWork_.mappedPinCount >=
                sizeof(configurationWork_.mappedPins) /
                    sizeof(configurationWork_.mappedPins[0]))
          return false;
        for (size_t i = 0; i < configurationWork_.mappedPinCount; ++i)
          if (configurationWork_.mappedPins[i] == pin) return false;
        configurationWork_.mappedPins[configurationWork_.mappedPinCount++] = pin;
      }
      return flova::copyBounded(unit.data.datastream.key,
                                configurationWork_.keyTarget, true);
    }
    if (unit.kind == flova::config::UnitKind::System) {
      if (configurationWork_.systemSeen) return false;
      configurationWork_.systemSeen = true;
      if (capabilities.automaticMapping && capabilities.statusIndicator &&
          unit.data.system.hasStatusLedPin) {
        configurationWork_.statusLedPin = unit.data.system.statusLedPin;
        for (size_t i = 0; i < configurationWork_.mappedPinCount; ++i)
          if (configurationWork_.mappedPins[i] == configurationWork_.statusLedPin)
            return false;
      }
      return true;
    }
    if (unit.kind == flova::config::UnitKind::Schedule) {
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
      return true;
    }
    if (unit.kind != flova::config::UnitKind::ScheduleOccurrences)
      return true;
    size_t index = validationScheduleCount_;
    for (size_t i = 0; i < validationScheduleCount_; ++i)
      if (validationSchedule(i).id == unit.data.occurrences.scheduleId) {
        index = i;
        break;
      }
    ValidationSchedule schedule =
        index < validationScheduleCount_ ? validationSchedule(index)
                                         : ValidationSchedule();
    if (index == validationScheduleCount_ ||
        !unit.data.occurrences.chunkCount ||
        unit.data.occurrences.chunkIndex >= unit.data.occurrences.chunkCount ||
        unit.data.occurrences.chunkIndex != schedule.nextChunk ||
        (schedule.chunkCount &&
         schedule.chunkCount != unit.data.occurrences.chunkCount) ||
        !unit.data.occurrences.occurrenceCount ||
        unit.data.occurrences.occurrenceCount >
            flova::config::kConfigurationOccurrenceChunk ||
        schedule.occurrences + unit.data.occurrences.occurrenceCount >
            FLOVA_SCHEDULE_OCCURRENCE_CAPACITY)
      return false;
    schedule.chunkCount = unit.data.occurrences.chunkCount;
    ++schedule.nextChunk;
    schedule.occurrences += unit.data.occurrences.occurrenceCount;
    setValidationSchedule(index, schedule);
    return true;
  }

  void stepConfigurationSemantic() {
    if (configurationWork_.sequence >= configurationWork_.manifest.recordCount) {
      for (size_t i = 0; i < validationScheduleCount_; ++i) {
        const ValidationSchedule schedule = validationSchedule(i);
        if (!schedule.chunkCount || schedule.nextChunk != schedule.chunkCount) {
          failConfigurationWork(true);
          return;
        }
      }
      configurationWork_.sequence = 0;
      configurationWork_.phase = ConfigurationWorkPhase::References;
      return;
    }
    if (!decodeGenerationUnit(configurationWork_.generation,
                              configurationWork_.sequence) ||
        !validateConfigurationUnit(configurationDecodeWorkspace_.typedUnit)) {
      failConfigurationWork(true);
      return;
    }
    if (configurationDecodeWorkspace_.typedUnit.kind ==
        flova::config::UnitKind::Datastream) {
      configurationWork_.priorSequence = 0;
      configurationWork_.phase = ConfigurationWorkPhase::KeyScan;
      return;
    }
    ++configurationWork_.sequence;
  }

  void stepConfigurationKeyScan() {
    if (configurationWork_.priorSequence < configurationWork_.sequence) {
      if (!decodeGenerationUnit(configurationWork_.generation,
                                configurationWork_.priorSequence)) {
        failConfigurationWork(true);
        return;
      }
      const flova::config::Unit& unit = configurationDecodeWorkspace_.typedUnit;
      if (unit.kind == flova::config::UnitKind::Datastream &&
          strcmp(unit.data.datastream.key, configurationWork_.keyTarget) == 0) {
        failConfigurationWork(true);
        return;
      }
      ++configurationWork_.priorSequence;
      return;
    }
    ++configurationWork_.sequence;
    configurationWork_.phase = ConfigurationWorkPhase::Semantic;
  }

  void stepConfigurationReferences() {
    if (configurationWork_.sequence >= configurationWork_.manifest.recordCount) {
      if (configurationWork_.mode == ConfigurationWorkMode::TransferVerify) {
        finishConfigurationVerification();
        return;
      }
      logger_.log("[flova] configuration generation validated");
      configurationWork_.sequence = 0;
      configurationWork_.phase = ConfigurationWorkPhase::PrepareSchedules;
      configurationWork_.scheduleCount = 0;
      configurationWork_.generatedAt = 0;
      configurationWork_.validUntil = 0;
      compilingSchedules_ = false;
      return;
    }
    if (!decodeGenerationUnit(configurationWork_.generation,
                              configurationWork_.sequence)) {
      failConfigurationWork(true);
      return;
    }
    const flova::config::Unit& unit = configurationDecodeWorkspace_.typedUnit;
    if (unit.kind == flova::config::UnitKind::Safety &&
        !hasValidatedDatastream(unit.data.safety.datastreamId)) {
      failConfigurationWork(true);
      return;
    }
    if (unit.kind == flova::config::UnitKind::Schedule)
      for (uint8_t action = 0; action < unit.data.schedule.actionCount; ++action)
        if (!hasValidatedDatastream(
                unit.data.schedule.actions[action].datastreamId)) {
          failConfigurationWork(true);
          return;
        }
    ++configurationWork_.sequence;
  }

  void stepConfigurationSchedules() {
    if (configurationWork_.sequence < configurationWork_.manifest.recordCount) {
      if (!configurationInstaller_.loadWorkspace(
              configurationWork_.generation, configurationWork_.sequence)) {
        failConfigurationWork(true);
        return;
      }
      const flova::config::Record& stored = configurationInstaller_.workspace();
      configurationDecodeWorkspace_ = FlovaLinkConfigurationRecord();
      if (!link_.decodeStoredConfigurationRecord(
              stored.body, stored.length, configurationDecodeWorkspace_) ||
          !configurationDecodeWorkspace_.hasTypedUnit) {
        failConfigurationWork(true);
        return;
      }
      const flova::config::Unit& unit = configurationDecodeWorkspace_.typedUnit;
      if (unit.kind == flova::config::UnitKind::Schedule) {
        if (configurationWork_.scheduleCount >= FLOVA_SCHEDULE_CAPACITY ||
            !unit.data.schedule.validUntil) {
          failConfigurationWork(true);
          return;
        }
        ++configurationWork_.scheduleCount;
        if (!configurationWork_.generatedAt ||
            unit.data.schedule.validFrom < configurationWork_.generatedAt)
          configurationWork_.generatedAt = unit.data.schedule.validFrom;
        if (!configurationWork_.validUntil ||
            unit.data.schedule.validUntil < configurationWork_.validUntil)
          configurationWork_.validUntil = unit.data.schedule.validUntil;
      }
      ++configurationWork_.sequence;
      return;
    }
    compilingSchedules_ = configurationWork_.scheduleCount != 0;
    if (compilingSchedules_) {
      static const uint64_t kRenewBeforeMs =
          14ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
      const uint64_t renewBefore = configurationWork_.validUntil > kRenewBeforeMs
                                       ? configurationWork_.validUntil - kRenewBeforeMs
                                       : configurationWork_.validUntil;
      if (!scheduleCompiler_.begin(
              configurationWork_.generation, configurationWork_.generatedAt,
              configurationWork_.validUntil, renewBefore,
              configurationWork_.scheduleCount)) {
        failConfigurationWork(false);
        return;
      }
    }
    configurationWork_.phase = ConfigurationWorkPhase::ApplyInit;
  }

  void stepConfigurationApply() {
    if (configurationWork_.sequence >= configurationWork_.manifest.recordCount) {
      configurationWork_.phase = ConfigurationWorkPhase::Finish;
      return;
    }
    if (!decodeGenerationUnit(configurationWork_.generation,
                              configurationWork_.sequence)) {
      hardware_.failSafe();
      failConfigurationWork(false);
      return;
    }
    const flova::config::Unit& unit = configurationDecodeWorkspace_.typedUnit;
    if (!device_.applyConfigurationUnit(unit) || !hardware_.apply(unit) ||
        !applyScheduleUnit(unit)) {
      hardware_.failSafe();
      failConfigurationWork(false);
      return;
    }
    ++configurationWork_.sequence;
  }

  void finishConfigurationApply() {
    if (compilingSchedules_ &&
        (!scheduleCompiler_.finish() ||
         (!configurationWork_.scheduleAlreadyInstalled &&
          !scheduleRuntime_.installPrepared()))) {
      hardware_.failSafe();
      failConfigurationWork(false);
      return;
    }
    if (!compilingSchedules_) scheduleRuntime_.clear();
    logger_.log("[flova] configuration generation applied");
    const ConfigurationWorkMode mode = configurationWork_.mode;
    activeConfigurationGeneration_ = configurationWork_.generation;
    memcpy(activeConfigurationChecksum_, configurationWork_.manifest.checksum.bytes,
           sizeof(activeConfigurationChecksum_));
    configurationCommitted_ = configurationWork_.generation != 0;
    link_.setConfigurationGeneration(configurationWork_.generation);
    configurationWork_ = ConfigurationWork();
    if (mode == ConfigurationWorkMode::BootstrapRestore ||
        mode == ConfigurationWorkMode::BootstrapApply) {
      finishBootstrapConfiguration();
      return;
    }
    lifecycle_ = FlovaLifecycle::WaitingForNetwork;
    beginSavedRuntime();
  }

  void failConfigurationWork(bool validationFailure) {
    const ConfigurationWorkMode mode = configurationWork_.mode;
    const uint32_t generation = configurationWork_.generation;
    const bool canTryPrevious =
        validationFailure &&
        (mode == ConfigurationWorkMode::BootRestore ||
         mode == ConfigurationWorkMode::BootstrapRestore) &&
        configurationWork_.candidateIndex < 2;
    if (canTryPrevious) {
      configurationStorage_.discardGeneration(generation);
      configurationWork_.phase = ConfigurationWorkPhase::SelectCandidate;
      return;
    }
    if (mode == ConfigurationWorkMode::TransferVerify) {
      const flova::config::Status status =
          configurationStorage_.discardGeneration(generation)
              ? flova::config::Status::VerificationFailure
              : flova::config::Status::StorageFailure;
      configurationInstaller_.reset();
      publishConfigurationReport(status, FlovaLinkConfigurationPhase::End,
                                 configurationWork_.bootstrapping);
      configurationWork_ = ConfigurationWork();
      return;
    }
    configurationWork_ = ConfigurationWork();
    writeError("configuration_restore_failed");
    lifecycle_ = FlovaLifecycle::Failed;
  }

  void finishConfigurationVerification() {
    const bool bootstrapping = configurationWork_.bootstrapping;
    const uint32_t generation = configurationWork_.generation;
    if (!configurationInstaller_.promote(generation)) {
      configurationStorage_.discardGeneration(generation);
      configurationInstaller_.reset();
      publishConfigurationReport(flova::config::Status::StorageFailure,
                                 FlovaLinkConfigurationPhase::End,
                                 bootstrapping);
      configurationWork_ = ConfigurationWork();
      return;
    }
    activeConfigurationGeneration_ = generation;
    memcpy(activeConfigurationChecksum_, configurationWork_.manifest.checksum.bytes,
           sizeof(activeConfigurationChecksum_));
    configurationVerifiedGeneration_ = generation;
    configurationCommitted_ = true;
    link_.setConfigurationGeneration(generation);
    logger_.log("[flova] configuration generation committed");
    publishConfigurationReport(flova::config::Status::Accepted,
                               FlovaLinkConfigurationPhase::End,
                               bootstrapping);
    configurationWork_ = ConfigurationWork();
    if (!bootstrapping) {
      link_.disconnect();
      requestRestart(FlovaRestartReason::ConfigurationActivation);
    }
  }

  void publishConfigurationReport(flova::config::Status status,
                                  FlovaLinkConfigurationPhase phase,
                                  bool bootstrapping) {
    configurationReportWorkspace_.status =
        status == flova::config::Status::Accepted ||
                status == flova::config::Status::Duplicate ||
                status == flova::config::Status::AlreadyCommitted
            ? FlovaLinkResultStatus::Ok
            : FlovaLinkResultStatus::Error;
    if (configurationReportWorkspace_.status == FlovaLinkResultStatus::Error) {
      const char* errorCode = configurationErrorCode(status);
      strncpy(configurationReportWorkspace_.errorCode, errorCode,
              sizeof(configurationReportWorkspace_.errorCode) - 1);
      configurationReportWorkspace_
          .errorCode[sizeof(configurationReportWorkspace_.errorCode) - 1] = 0;
      char message[128] = {};
      snprintf(message, sizeof(message),
               "[flova] configuration rejected phase=%u generation=%lu sequence=%lu status=%u",
               static_cast<unsigned>(phase),
               static_cast<unsigned long>(configurationReportWorkspace_.generation),
               static_cast<unsigned long>(configurationReportWorkspace_.sequence),
               static_cast<unsigned>(status));
      logger_.log(message);
    }
    link_.publishConfigurationReport(configurationReportWorkspace_);
    if (phase == FlovaLinkConfigurationPhase::End &&
        configurationReportWorkspace_.status == FlovaLinkResultStatus::Ok) {
      configurationCommitted_ = true;
      activeConfigurationGeneration_ = configurationReportWorkspace_.generation;
      memcpy(activeConfigurationChecksum_, configurationReportWorkspace_.checksum,
             sizeof(activeConfigurationChecksum_));
      link_.setConfigurationGeneration(activeConfigurationGeneration_);
    }
    (void)bootstrapping;
  }

  void completeConfigurationRestore() {
    const ConfigurationWorkMode mode = configurationWork_.mode;
    configurationWork_ = ConfigurationWork();
    if (mode == ConfigurationWorkMode::BootstrapRestore ||
        mode == ConfigurationWorkMode::BootstrapApply) {
      finishBootstrapConfiguration();
      return;
    }
    lifecycle_ = FlovaLifecycle::WaitingForNetwork;
    beginSavedRuntime();
  }

  void finishBootstrapConfiguration() {
    if (!link_.configure(runtimeConfiguration_.linkUrl,
                         runtimeConfiguration_.deviceId,
                         runtimeConfiguration_.linkSecret)) {
      writeError("runtime_link_configuration_failed");
      lifecycle_ = FlovaLifecycle::Failed;
      return;
    }
    beginDeviceRuntime();
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
  static_assert(kValidationScheduleOffset +
                        FLOVA_SCHEDULE_CAPACITY * sizeof(ValidationSchedule) <=
                    sizeof(flova::ConfigurationImage),
                "configuration validation workspace exceeds the shared image buffer");

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
    if (!link_.takeOtaOffer(otaOfferWorkspace_)) return;
    const FlovaLinkOtaOffer& offer = otaOfferWorkspace_;
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
    const uint32_t maxImageBytes = link_.otaMaxImageBytes();
    if (!maxImageBytes || offer.sizeBytes > maxImageBytes) {
      otaResult_.status = FlovaLinkResultStatus::Error;
      strncpy(otaResult_.errorCode, "ota_image_too_large",
              sizeof(otaResult_.errorCode) - 1);
      otaResultPending_ = true;
      return;
    }
    if (!offer.installId.present || !offer.releaseId.present || !offer.sizeBytes ||
        strncmp(offer.url, "https://", 8) != 0 ||
        (offer.firmwareTarget[0] && provisioningConfig_.firmwareTarget &&
         strcmp(offer.firmwareTarget, provisioningConfig_.firmwareTarget) != 0)) {
      otaResult_.status = FlovaLinkResultStatus::Error;
      strncpy(otaResult_.errorCode, "ota_offer_invalid",
              sizeof(otaResult_.errorCode) - 1);
      otaResultPending_ = true;
      return;
    }
    otaPendingRecord_ = {};
    otaPendingRecord_.magic = kOtaPendingMagic;
    otaPendingRecord_.installId = offer.installId;
    otaPendingRecord_.releaseId = offer.releaseId;
    strncpy(otaPendingRecord_.version, offer.version,
            sizeof(otaPendingRecord_.version) - 1);
    if (!storage_.write("ota_pending", &otaPendingRecord_, sizeof(otaPendingRecord_))) {
      otaResult_.status = FlovaLinkResultStatus::Error;
      strncpy(otaResult_.errorCode, "storage_failed",
              sizeof(otaResult_.errorCode) - 1);
      otaResultPending_ = true;
      return;
    }
    otaPendingRecordValid_ = true;
    FlovaLinkOtaReport accepted = {};
    accepted.messageId = nextControlMessageId();
    accepted.installId = offer.installId;
    accepted.status = FlovaLinkResultStatus::Accepted;
    strncpy(accepted.errorCode, "ok", sizeof(accepted.errorCode) - 1);
    if (!link_.publishOtaReport(accepted)) {
      storage_.remove("ota_pending");
      otaPendingRecordValid_ = false;
      return;
    }
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
    storage_.remove("ota_pending");
    otaPendingRecordValid_ = false;
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
      if (otaActivationFailurePending_) {
        otaResult_ = {};
        otaResult_.messageId = nextControlMessageId();
        otaResult_.installId = otaPendingRecord_.installId;
        otaResult_.status = FlovaLinkResultStatus::Error;
        strncpy(otaResult_.errorCode, "ota_activation_failed",
                sizeof(otaResult_.errorCode) - 1);
        if (!link_.publishOtaReport(otaResult_)) return;
        storage_.remove("ota_pending");
        otaPendingRecordValid_ = false;
        otaActivationFailurePending_ = false;
      }
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
    const uint32_t maxImageBytes = link_.otaMaxImageBytes();
    heartbeat.otaCapable = otaEnabled_ && maxImageBytes > 0;
    heartbeat.otaMaxImageBytes = heartbeat.otaCapable ? maxImageBytes : 0;
    heartbeat.otaStrategy = heartbeat.otaCapable
                                ? link_.otaStrategy()
                                : FlovaOtaStrategy::None;
    heartbeat.otaRollbackCapable = heartbeat.otaCapable &&
                                   link_.otaRollbackCapable();
    heartbeat.otaBootState = heartbeat.otaCapable ? otaBootState_ : FlovaOtaBootState::Stable;
    if (heartbeat.otaBootState == FlovaOtaBootState::RolledBack)
      copy(heartbeat.otaRollbackReason, otaRollbackReason_);
    if (!copy(heartbeat.otaBootLayoutVersion,
              heartbeat.otaCapable ? link_.otaBootLayoutVersion() : "legacy"))
      return;
    strncpy(heartbeat.firmwareVersion, FLOVA_FIRMWARE_VERSION,
            sizeof(heartbeat.firmwareVersion) - 1);
    if (provisioningConfig_.firmwareTarget) {
      strncpy(heartbeat.firmwareTarget,
              provisioningConfig_.firmwareTarget,
              sizeof(heartbeat.firmwareTarget) - 1);
    }
    if (otaPendingRecordValid_ &&
        strcmp(otaPendingRecord_.version, FLOVA_FIRMWARE_VERSION) == 0) {
      heartbeat.runningReleaseId = otaPendingRecord_.releaseId;
      heartbeat.lastInstallId = otaPendingRecord_.installId;
    }
    if (link_.publishHeartbeat(heartbeat)) lastHeartbeatAt_ = now ? now : 1;
  }

  uint64_t nextControlMessageId() {
    return device_.originateMessageId();
  }

  void failBootstrap(const char* error) {
    flova::markProvisioningFailure(pending_, error);
    writeError(error);
    char retryReason[flova::kProvisioningErrorBytes] = {};
    strncpy(retryReason, pending_.lastError, sizeof(retryReason) - 1);
    link_.disconnect();
    if (flova::terminalProvisioningError(error) ||
        pending_.attempts >= kMaximumBootstrapAttempts) {
      resetPendingConfiguration();
      if (!storage_.remove("prov_pending")) writeError("storage_failed");
      provisioningFallback();
      return;
    }
    if (!storage_.write("prov_pending", &pending_, sizeof(pending_)) ||
        !markBootstrapAttempt()) {
      writeError("storage_failed");
      provisioningFallback();
      return;
    }
    char retryMessage[128] = {};
    snprintf(retryMessage, sizeof(retryMessage),
             "[flova] bootstrap retry attempt=%u/%u reason=%s",
             static_cast<unsigned>(pending_.attempts),
             static_cast<unsigned>(kMaximumBootstrapAttempts),
             retryReason);
    logger_.log(retryMessage);
    lifecycle_ = FlovaLifecycle::WaitingForNetwork;
    bootstrapStartedAt_ = millis();
  }

  void resetPendingConfiguration() {
    flova::config::GenerationManifest pending = {};
    uint32_t active = 0;
    if (configurationStorage_.pendingManifest(pending) &&
        configurationStorage_.activeGeneration(active) &&
        pending.generation > active)
      configurationStorage_.discardGeneration(pending.generation);
    configurationInstaller_.reset();
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
      failBootstrap("configuration_verification_failed");
      return;
    }
    if (!storage_.remove("prov_pending") || !storage_.remove("prov_error")) {
      failBootstrap("configuration_cleanup_failed");
      return;
    }
    link_.disconnect();
    memset(&pending_, 0, sizeof(pending_));
    if (configurationVerifiedGeneration_ == committed.generation) {
      startConfigurationRestore(ConfigurationWorkMode::BootstrapApply,
                                committed.generation);
    } else {
      startConfigurationRestore(ConfigurationWorkMode::BootstrapRestore,
                                committed.generation);
    }
  }

  void serviceOtaBoot() {
    if (otaBootState_ != FlovaOtaBootState::Candidate) return;
    if (!link_.connected() || !device_.ready()) {
      otaHealthStartedAt_ = 0;
      return;
    }
    const uint32_t now = millis();
    if (!otaHealthStartedAt_) otaHealthStartedAt_ = now ? now : 1;
    const uint32_t elapsed = now - otaHealthStartedAt_;
    if (elapsed >= kOtaHealthDeadlineMs) {
      copy(otaRollbackReason_, "ota_health_timeout");
      link_.rollbackOtaBoot();
      return;
    }
    if (elapsed >= kOtaHealthWindowMs) {
      if (link_.confirmOtaBoot()) {
        otaBootState_ = FlovaOtaBootState::Stable;
        otaHealthStartedAt_ = 0;
      }
      return;
    }
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

  static bool validFirmwareTarget(const char* target) {
    if (!target || !target[0]) return false;
    const size_t length = strlen(target);
    if (length >= FLOVA_LINK_OTA_TARGET_BYTES) return false;
    for (size_t i = 0; i < length; ++i) {
      const char c = target[i];
      const bool alpha = c >= 'a' && c <= 'z';
      const bool digit = c >= '0' && c <= '9';
      if (!alpha && !digit && c != '_' && c != '-' && c != '.') return false;
    }
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
      if (identity_.hardwareId(hardwareIdWorkspace_, sizeof(hardwareIdWorkspace_)))
        provisioningConfig_.hardwareId = hardwareIdWorkspace_;
    }
    if (!provisioningConfig_.firmwareTarget || !provisioningConfig_.firmwareTarget[0]) {
      provisioningConfig_.firmwareTarget = identity_.firmwareTarget();
    }
  }

  ProvisioningConfig provisioningConfig_;
  FlovaClientLink& link_;
  FlovaProvisioningAdapter& provisioning_;
  FlovaNetworkRuntime& network_;
  FlovaTlsClockBootstrap& tlsClock_;
  FlovaBoardIdentity& identity_;
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
  uint32_t configurationVerifiedGeneration_ = 0;
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
  FlovaLinkOtaOffer otaOfferWorkspace_ = {};
  bool otaResultPending_ = false;
  bool otaEnabled_ = false;
  FlovaOtaPendingRecord otaPendingRecord_ = {};
  bool otaPendingRecordValid_ = false;
  bool otaActivationFailurePending_ = false;
  FlovaOtaBootState otaBootState_ = FlovaOtaBootState::Stable;
  char otaRollbackReason_[FLOVA_LINK_TEXT_BYTES] = {};
  uint32_t otaHealthStartedAt_ = 0;
  size_t validationDatastreamCount_ = 0;
  size_t validationScheduleCount_ = 0;
  ConfigurationWork configurationWork_ = {};
  FlovaRestartHandler restartHandler_ = nullptr;
  void* restartContext_ = nullptr;
  FlovaRestartReason restartReason_ = FlovaRestartReason::None;
  uint32_t factoryResetRequestedAt_ = 0;
  char factoryResetCommandId_[FLOVA_LINK_TEXT_BYTES] = {};
};
