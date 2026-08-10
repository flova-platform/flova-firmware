#pragma once

#include "FlovaFactoryResetGesture.h"
#include "FlovaClock.h"
#include "FlovaLogger.h"
#include "FlovaStorage.h"
#include "FlovaTransport.h"
#include "FlovaTypes.h"
#include "FlovaBuildConfig.h"
#include "FlovaOta.h"
#include "FlovaBootControl.h"

class FlovaDevice {
 public:
  FlovaDevice(FlovaTransport& transport, FlovaStorage& storage, FlovaClock& clock, FlovaLogger& logger)
      : transport_(transport), storage_(storage), clock_(clock), logger_(logger) {}

  void configure(const FlovaConfig& config);
  bool begin();
  void loop();
  template <typename T> class Datastream;
  template <typename T> Datastream<T> datastream(const char* key);
  void addDigitalOutput(const char* key, uint8_t pin, bool activeHigh = true, uint32_t minOutputIntervalMs = 300);
  void addDigitalInput(const char* key, uint8_t pin, bool activeHigh = true, uint32_t debounceMs = 50, uint8_t mode = INPUT);
  void addAnalogInput(const char* key, uint8_t pin, uint32_t sampleIntervalMs = 1000);
  void addPwmOutput(const char* key, uint8_t pin, double minimum = 0, double maximum = 100,
                    double initialValue = 0);
  FlovaWriteResult applyWrite(const char* key, const String& value, FlovaValueType type,
                              FlovaValueOrigin origin = FlovaValueOrigin::LocalLogic,
                              const FlovaLinkId& commandId = FlovaLinkId(),
                              const FlovaLinkId& correlationId = FlovaLinkId(),
                              uint32_t desiredVersion = 0, bool acknowledgeCloud = false);
  FlovaWriteResult reportValue(const char* key, const String& value, FlovaValueType type,
                               FlovaValueOrigin origin = FlovaValueOrigin::SensorRead);
  bool readCached(const char* key, String& value, uint32_t* revision = nullptr) const;
  bool readSnapshotMetadata(const char* key, uint32_t& updatedAt, FlovaValueOrigin& origin,
                            FlovaValueQuality& quality, bool& dirty, uint32_t& revision) const;
  bool hasValue(const char* key) const;
  bool registerTypedWrite(const char* key, void* handler, FlovaValueType type);
  bool registerTypedRead(const char* key, void* handler, FlovaValueType type);
  void configureDatastream(const char* key, FlovaDatastreamMode mode, FlovaOfflinePolicy offline,
                           FlovaPersistencePolicy persistence, FlovaRestorePolicy restore);
  void configurePublishInterval(const char* key, uint32_t minimumIntervalMs);
  void enableStateBatching(uint8_t maximumReadings = 32, uint32_t flushIntervalMs = 100);
  void setStatusLed(uint8_t pin, bool activeLow);
  void setFactoryResetButton(uint8_t pin, bool activeLow, uint32_t holdMs = 10000,
                             uint8_t mode = 255);
  bool isOtaCapable() const { return config_.otaCapable; }
  void setOtaInstaller(FlovaOtaInstaller& installer) { otaInstaller_ = &installer; }
  void setBootControl(FlovaBootControl& control) { bootControl_ = &control; }
  void factoryReset();
  void handleMessage(const FlovaLinkInboundMessage& message);
  bool beginTransportOnly();
  uint32_t activeConfigurationGeneration() const { return configurationGeneration_; }
  // Platform code receives one schema-decoded record at a time and must stage
  // it before returning success. It must never receive a JSON configuration.
  virtual bool applyConfigurationRecord(const FlovaLinkConfigurationRecord&) { return false; }
  // Called from loop(), never from a transport callback. Board glue applies
  // one already-decoded bounded unit and may fail closed on hardware errors.
  virtual bool applyConfigurationUnit(const flova::config::Unit&);
  virtual bool restoreActiveConfiguration() { return true; }
  virtual bool applyScheduleRecord(const FlovaLinkScheduleRecord&) { return false; }
  virtual void onBootstrapCommitted(const FlovaLinkBootstrapCommitted&) {}
  virtual void onConfigurationCommitted(uint32_t) {}
  virtual void onRuntimeRestoreBegin() {}
  virtual void onRuntimeRestoreComplete(bool) {}
  void setConfigurationApplyingGeneration(uint32_t generation) {
    configurationApplyingGeneration_ = generation;
    configurationGeneration_ = generation;
    transport_.setConfigurationGeneration(generation);
  }
  void setActiveConfiguration(uint32_t generation, const uint8_t checksum[32],
                              bool valid = true);

 protected:
  // Bootstrap board loops may own transport polling and must still drain the
  // staged configuration unit before the next Link frame arrives.
  void applyPendingConfiguration();
  void deferConfigurationRuntime(bool defer) { configurationRuntimeDeferred_ = defer; }
  bool configurationRuntimeDeferred() const { return configurationRuntimeDeferred_; }

 private:
  struct DatastreamState {
    String key;
    String value;
    FlovaValueType type = FlovaValueType::String;
    FlovaDatastreamMode mode = FlovaDatastreamMode::State;
    FlovaOfflinePolicy offline = FlovaOfflinePolicy::KeepLatest;
    FlovaPersistencePolicy persistence = FlovaPersistencePolicy::None;
    FlovaRestorePolicy restore = FlovaRestorePolicy::DoNotRestore;
    FlovaValueOrigin origin = FlovaValueOrigin::Unknown;
    FlovaValueQuality quality = FlovaValueQuality::Stale;
    uint32_t updatedAt = 0, revision = 0;
    uint32_t minimumPublishIntervalMs = 0, lastPublishedMs = 0;
    uint32_t batchedRevision = 0;
    uint32_t lastDesiredVersion = 0;
    uint8_t safetyPolicy = 0;
    bool hasSafetyMinimum = false, hasSafetyMaximum = false;
    String safetyMinimum;
    String safetyMaximum;
    bool hasValue = false, dirty = false, publishPending = false, batchInFlight = false;
    void* writeHandler = nullptr;
    void* readHandler = nullptr;
  };
  struct DigitalOutput {
    String key;
    uint8_t pin = 255;
    bool activeHigh = true;
    bool value = false;
    bool pending = false;
    bool pendingValue = false;
    FlovaLinkId pendingCommandId = {};
    FlovaLinkId pendingCorrelationId = {};
    uint32_t minOutputIntervalMs = 300;
    uint32_t lastAppliedMs = 0;
    uint32_t lastAppliedDesiredVersion = 0;
  };
  struct DigitalInput {
    String key;
    uint8_t pin = 255;
    bool activeHigh = true;
    uint32_t debounceMs = 50;
    bool lastRaw = false;
    bool lastSent = false;
    uint32_t changedAt = 0;
  };
  struct AnalogInput {
    String key;
    uint8_t pin = 255;
    uint32_t sampleIntervalMs = 1000;
    uint32_t lastSampleMs = 0;
    bool sampled = false;
  };
  struct PwmOutput {
    String key;
    uint8_t pin = 255;
    double minimum = 0;
    double maximum = 100;
  };
  struct PendingBatchReading {
    uint8_t stateIndex = 0;
    uint32_t revision = 0;
    char value[FLOVA_TEXT_CAPACITY] = {};
  };

  void initializeResourceContract();
  void updateStatusIndicator();
  void processFactoryResetGesture();
  bool publishHeartbeat();
  bool publishConfigurationState();
  bool reconnect();
  bool datastreamAllowed(const char* key) const;
  DatastreamState* stateFor(const char* key, FlovaValueType type, bool create);
  const DatastreamState* stateFor(const char* key) const;
  bool valueMatchesType(const String& value, FlovaValueType type) const;
  void updateState(DatastreamState& state, const String& value, FlovaValueOrigin origin, bool dirty);
  void persistState(const DatastreamState& state);
  void restorePersistentStates();
  bool publishState(DatastreamState& state);
  bool publishStateBatch();
  void flushDueStates();
  uint64_t stateMessageId(const DatastreamState& state) const;
  void flushDirtyStates();
  FlovaWriteResult invokeWriteHandler(DatastreamState& state, const String& value,
                                      const FlovaLinkId& commandId = FlovaLinkId(),
                                      const FlovaLinkId& correlationId = FlovaLinkId());
  bool handleMappedWrite(const FlovaLinkId& commandId, const FlovaLinkId& correlationId, const String& key,
                         const String& value, uint32_t desiredVersion);
  void handleConfiguration(const FlovaLinkConfigurationRecord& record);
  void handleOtaOffer(const FlovaLinkOtaOffer& offer);
  void processPendingOta();
  void updateCandidateHealth();
  void reportOta(FlovaLinkResultStatus status, const char* errorCode = nullptr);
  void applyDigitalOutput(DigitalOutput& output, bool value);
  void ackDigitalOutput(const DigitalOutput& output, const FlovaLinkId& commandId, const FlovaLinkId& correlationId);
  void flushDigitalOutputs();
  void pollDigitalInputs();
  void pollAnalogInputs();
  FlovaWriteResult applyPwmOutput(PwmOutput& output, const String& value);
  bool commandSeen(const FlovaLinkId& commandId) const;
  void rememberCommand(const FlovaLinkId& commandId);
  void acknowledgeDuplicateCommand(const FlovaLinkId& commandId, const FlovaLinkId& correlationId,
                                   const String& key, const String& value);
  void requestTimeSync();
  void handleTimeSync(const FlovaLinkTimeResponse& response);
  void handleIngestionAck(const FlovaLinkAcknowledgement& acknowledgement);
  void handleIngestionRetry(const FlovaLinkAcknowledgement& acknowledgement);
  void handleScheduleRecord(const FlovaLinkScheduleRecord& record);
  void restoreScheduleManifest();
  void runSchedules();
  void reportScheduleStatus(const char* status);
  void requestScheduleRenewal();
  bool applyHardwareMapping(const char* key, const flova::config::HardwareMapping& mapping,
                            double minimum = 0, double maximum = 100,
                            bool hasRange = false);

  FlovaTransport& transport_;
  FlovaStorage& storage_;
  FlovaClock& clock_;
  FlovaLogger& logger_;
  FlovaOtaInstaller* otaInstaller_ = nullptr;
  FlovaBootControl* bootControl_ = nullptr;
  uint32_t candidateStartedMs_ = 0;
  uint32_t candidateHealthySinceMs_ = 0;
  bool candidateHeartbeatPublished_ = false;
  FlovaLinkOtaOffer pendingOtaOffer_ = {};
  bool pendingOta_ = false;
  FlovaConfig config_;
  DatastreamState states_[FLOVA_DATASTREAM_CAPACITY];
  uint8_t stateCount_ = 0;
  DigitalOutput outputs_[FLOVA_HARDWARE_OUTPUT_CAPACITY];
  uint8_t outputCount_ = 0;
  DigitalInput inputs_[FLOVA_HARDWARE_INPUT_CAPACITY];
  uint8_t inputCount_ = 0;
  AnalogInput analogInputs_[FLOVA_HARDWARE_INPUT_CAPACITY];
  uint8_t analogInputCount_ = 0;
  PwmOutput pwmOutputs_[FLOVA_HARDWARE_OUTPUT_CAPACITY];
  uint8_t pwmOutputCount_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  uint32_t bootNonce_ = 0;
  uint32_t lastReconnectMs_ = 0;
  uint32_t lastCriticalRetryMs_ = 0;
  uint32_t batchSequence_ = 0;
  uint32_t batchQueuedAtMs_ = 0;
  uint32_t batchLastPublishedMs_ = 0;
  uint32_t batchFlushIntervalMs_ = 100;
  uint8_t batchMaximumReadings_ = 0;
  uint64_t batchMessageId_ = 0;
  PendingBatchReading batchReadings_[FLOVA_DATASTREAM_CAPACITY];
  uint8_t batchReadingCount_ = 0;
  uint32_t reconnectDelayMs_ = 1000;
  uint32_t reconnectBackoffCeilingMs_ = 1000;
  uint32_t connectedSinceMs_ = 0;
  uint32_t ingestionRetryNotBeforeMs_ = 0;
  uint8_t statusLedPin_ = 255;
  bool statusLedActiveLow_ = false;
  FlovaStatusIndicatorState statusIndicatorState_ = FlovaStatusIndicatorState::Offline;
  uint8_t resetButtonPin_ = 255;
  bool resetButtonActiveLow_ = false;
  FlovaFactoryResetGesture resetGesture_;
  uint32_t resetTapFlashUntilMs_ = 0;
  FlovaLinkId recentCommandIds_[FLOVA_COMMAND_DEDUP_CAPACITY] = {};
  uint8_t recentCommandCursor_ = 0;
  uint32_t lastTimeRequestMs_ = 0;
  uint32_t timeRequestStartedMs_ = 0;
  uint64_t pendingTimeRequestId_ = 0;
  uint32_t configurationGeneration_ = 0;
  uint8_t configurationChecksum_[32] = {};
  bool configurationValid_ = false;
  uint64_t scheduleRevision_ = 0;
  uint64_t scheduleValidUntil_ = 0;
  uint64_t scheduleRenewBefore_ = 0;
  uint64_t lastScheduleRenewRequest_ = 0;
  bool scheduleExpiryReported_ = false;
  bool skipPastScheduleOccurrences_ = false;
  bool configurationApplyPending_ = false;
  bool configurationRuntimeDeferred_ = false;
  uint32_t configurationApplyingGeneration_ = 0;
  FlovaLinkConfigurationRecord pendingConfiguration_ = {};
};

#include "FlovaDatastream.h"
