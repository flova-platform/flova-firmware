#pragma once
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
  FlovaWriteResult applyWrite(const char* key, const String& value, FlovaValueType type,
                              FlovaValueOrigin origin = FlovaValueOrigin::LocalLogic,
                              const String& commandId = "", const String& correlationId = "",
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
  void setStatusLed(uint8_t pin, bool activeLow);
  void setFactoryResetButton(uint8_t pin, bool activeLow, uint32_t holdMs = 10000);
  bool isOtaCapable() const { return config_.otaCapable; }
  void setOtaInstaller(FlovaOtaInstaller& installer) { otaInstaller_ = &installer; }
  void setBootControl(FlovaBootControl& control) { bootControl_ = &control; }
  void factoryReset();
  void handleMessage(const String& topic, const String& payload);
  virtual bool installRuntimeConfig(const String&) { return false; }

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
    uint32_t lastDesiredVersion = 0;
    bool hasValue = false, dirty = false;
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
    String pendingCommandId;
    String pendingCorrelationId;
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

  String topic(const char* suffix) const;
  String datastreamTopic(const char* key, const char* suffix) const;
  String heartbeatPayload() const;
  void initializeResourceContract();
  void updateStatusIndicator();
  bool publishHeartbeat();
  bool reconnect();
  String jsonValue(const char* key, const String& payload) const;
  String jsonScalar(const String& value) const;
  bool datastreamAllowed(const char* key) const;
  DatastreamState* stateFor(const char* key, FlovaValueType type, bool create);
  const DatastreamState* stateFor(const char* key) const;
  bool valueMatchesType(const String& value, FlovaValueType type) const;
  void updateState(DatastreamState& state, const String& value, FlovaValueOrigin origin, bool dirty);
  void persistState(const DatastreamState& state);
  void restorePersistentStates();
  bool publishState(DatastreamState& state);
  void flushDirtyStates();
  FlovaWriteResult invokeWriteHandler(DatastreamState& state, const String& value);
  bool handleMappedWrite(const String& commandId, const String& correlationId, const String& key, const String& value, const String& desiredVersion);
  void handleConfigSet(const String& payload);
  void handleOtaOffer(const String& payload);
  void processPendingOta();
  void updateCandidateHealth();
  void reportOta(const char* status, const char* errorCode = nullptr);
  void applyDigitalOutput(DigitalOutput& output, bool value);
  void ackDigitalOutput(const DigitalOutput& output, const String& commandId, const String& correlationId);
  void flushDigitalOutputs();
  void pollDigitalInputs();
  bool commandSeen(const String& commandId) const;
  void rememberCommand(const String& commandId);
  void acknowledgeDuplicateCommand(const String& commandId, const String& correlationId,
                                   const String& key, const String& value);
  void requestTimeSync();
  void handleTimeSync(const String& payload);
  bool installScheduleManifest(const String& payload, bool persist = true);
  void restoreScheduleManifest();
  void runSchedules();
  void reportScheduleStatus(const char* status);
  void requestScheduleRenewal();

  struct OfflineSchedule {
    String id;
    String key;
    String value;
    uint64_t firstUtcMs = 0;
    uint16_t minuteDeltas[FLOVA_SCHEDULE_OCCURRENCE_CAPACITY] = {};
    uint8_t deltaCount = 0;
    uint8_t cursor = 0;
    bool enabled = false;
  };

  FlovaTransport& transport_;
  FlovaStorage& storage_;
  FlovaClock& clock_;
  FlovaLogger& logger_;
  FlovaOtaInstaller* otaInstaller_ = nullptr;
  FlovaBootControl* bootControl_ = nullptr;
  uint32_t candidateStartedMs_ = 0;
  uint32_t candidateHealthySinceMs_ = 0;
  bool candidateHeartbeatPublished_ = false;
  String pendingOtaPayload_;
  FlovaConfig config_;
  DatastreamState states_[FLOVA_DATASTREAM_CAPACITY];
  uint8_t stateCount_ = 0;
  DigitalOutput outputs_[FLOVA_HARDWARE_OUTPUT_CAPACITY];
  uint8_t outputCount_ = 0;
  DigitalInput inputs_[FLOVA_HARDWARE_INPUT_CAPACITY];
  uint8_t inputCount_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  uint32_t lastReconnectMs_ = 0;
  uint32_t reconnectDelayMs_ = 1000;
  uint8_t statusLedPin_ = 255;
  bool statusLedActiveLow_ = false;
  FlovaStatusIndicatorState statusIndicatorState_ = FlovaStatusIndicatorState::Offline;
  uint8_t resetButtonPin_ = 255;
  bool resetButtonActiveLow_ = false;
  uint32_t resetHoldMs_ = 10000;
  uint32_t resetStartedMs_ = 0;
  String recentCommandIds_[FLOVA_COMMAND_DEDUP_CAPACITY];
  uint8_t recentCommandCursor_ = 0;
  uint32_t lastTimeRequestMs_ = 0;
  uint32_t timeRequestStartedMs_ = 0;
  String pendingTimeRequestId_;
#if FLOVA_SCHEDULE_RUNTIME_ENABLED
  OfflineSchedule offlineSchedules_[FLOVA_SCHEDULE_CAPACITY];
#endif
  uint8_t offlineScheduleCount_ = 0;
  uint64_t scheduleRevision_ = 0;
  uint64_t scheduleValidUntil_ = 0;
  uint64_t scheduleRenewBefore_ = 0;
  uint64_t lastScheduleRenewRequest_ = 0;
  bool scheduleExpiryReported_ = false;
  bool skipPastScheduleOccurrences_ = false;
};

#include "FlovaDatastream.h"
