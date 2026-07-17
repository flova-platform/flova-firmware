#pragma once
#include <Arduino.h>

struct FlovaCapabilities {
  uint16_t datastreamSlots = 0;
  uint16_t hardwareInputSlots = 0;
  uint16_t hardwareOutputSlots = 0;
  uint16_t commandDedupSlots = 0;
  uint16_t scheduleSlots = 0;
  uint32_t messageBytes = 0;
  uint32_t persistentBytes = 0;
  uint32_t scheduleManifestBytes = 0;
  uint32_t historyBytes = 0;
  bool scheduleChunks = false;
};

struct FlovaLimits {
  uint16_t datastreams = 0;
  uint16_t hardwareInputs = 0;
  uint16_t hardwareOutputs = 0;
  uint16_t commandDedup = 0;
  uint32_t messageBytes = 0;
  uint32_t scheduleManifestBytes = 0;
  uint16_t scheduleRenewBeforeDays = 0;
};

struct FlovaConfig {
  const char* deviceId = "";
  const char* mqttHost = "";
  uint16_t mqttPort = 1883;
  const char* mqttUsername = "";
  const char* mqttPassword = "";
  const char* firmwareVersion = "0.1.0";
  const char* firmwareTarget = "custom";
  const char* runningReleaseId = "";
  const char* lastInstallId = "";
  const char* sdkVersion = "0.1.0";
  const char* protocolName = "flova";
  uint16_t protocolVersion = 1;
  uint16_t schemaVersion = 1;
  const char* boardType = "esp32";
  bool otaCapable = false;
  bool rollbackCapable = false;
  uint32_t heartbeatIntervalMs = 30000;
  uint32_t flashSize = 0;
  const char* datastreamKeys = "";
  const char* appliedTemplateVersionId = "";
  const char* configChecksum = "";
  FlovaCapabilities capabilities;
  FlovaLimits limits;
};

// Mode describes delivery semantics, not the C++ value type.
enum class FlovaDatastreamMode : uint8_t { State, Sample, Command, Event };
enum class FlovaOfflinePolicy : uint8_t { KeepLatest, StoreHistory, Drop, Reject };
enum class FlovaPersistencePolicy : uint8_t { None, Persistent };
enum class FlovaRestorePolicy : uint8_t { DoNotRestore, RestoreCacheOnly, RestoreHardwareState };
enum class FlovaValueOrigin : uint8_t { Unknown, LocalLogic, SensorRead, PhysicalInput, UserCommand, CloudAutomation, CloudSync, DeviceRestore, Provisioning, Internal };
enum class FlovaValueQuality : uint8_t { Good, Stale, Invalid, Uncertain, HardwareError };
enum class FlovaWriteStatus : uint8_t { Accepted, Rejected, NoChange, Failed };
enum class FlovaValueType : uint8_t { Bool, Float, Number, String };
enum class FlovaStatusIndicatorState : uint8_t { Offline, Online };

struct FlovaWriteResult {
  FlovaWriteStatus status = FlovaWriteStatus::Failed;
  const char* reasonCode = "write_failed";
  const char* message = "";
  FlovaWriteResult() = default;
  FlovaWriteResult(FlovaWriteStatus status, const char* reason, const char* message) : status(status), reasonCode(reason), message(message) {}
  static FlovaWriteResult accept() { return FlovaWriteResult(FlovaWriteStatus::Accepted, "", ""); }
  static FlovaWriteResult reject(const char* reason, const char* message = "") { return FlovaWriteResult(FlovaWriteStatus::Rejected, reason, message); }
  static FlovaWriteResult noChange() { return FlovaWriteResult(FlovaWriteStatus::NoChange, "", ""); }
  static FlovaWriteResult failure(const char* reason, const char* message = "") { return FlovaWriteResult(FlovaWriteStatus::Failed, reason, message); }
  bool accepted() const { return status == FlovaWriteStatus::Accepted || status == FlovaWriteStatus::NoChange; }
};

template <typename T> struct FlovaReadResult {
  T value{};
  const char* reasonCode = "";
  bool ok = false;
  static FlovaReadResult success(const T& value) { FlovaReadResult result; result.value = value; result.ok = true; return result; }
  static FlovaReadResult error(const char* reason) { FlovaReadResult result; result.reasonCode = reason; return result; }
};

template <typename T> struct FlovaDatastreamSnapshot {
  T value{};
  bool hasValue = false;
  uint32_t updatedAt = 0;
  FlovaValueOrigin origin = FlovaValueOrigin::Unknown;
  FlovaValueQuality quality = FlovaValueQuality::Stale;
  bool stale = true;
  bool dirty = false;
  uint32_t revision = 0;
};
