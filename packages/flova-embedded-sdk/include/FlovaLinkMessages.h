#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "FlovaBuildConfig.h"
#include "FlovaConfigurationRuntime.h"
#include "FlovaDatastreamId.h"

// Internal wire-record transport used by the Arduino codec implementation.
// Public applications implement flova::Link from FlovaDevice.h instead.
// The SDK deliberately exposes domain records, never a route or serialized
// payload.  A platform Link implementation owns CBOR and the outer frame.
static const size_t FLOVA_LINK_TEXT_BYTES = FLOVA_TEXT_CAPACITY;
static const size_t FLOVA_LINK_OTA_URL_BYTES = 257;
static const size_t FLOVA_LINK_OTA_SHA256_BYTES = 65;
static const size_t FLOVA_LINK_OTA_VERSION_BYTES = 33;
static const size_t FLOVA_LINK_OTA_TARGET_BYTES = 33;
static const size_t FLOVA_LINK_RECORD_BYTES = 448;
// Four worst-case bounded text scalar items fit under the 500-byte payload
// limit after deterministic-CBOR array overhead. The encoder still enforces
// the final 512-byte frame invariant.
static const uint8_t FLOVA_LINK_MAX_STATE_READINGS = 4;

enum class FlovaLinkMessageType : uint8_t {
  State,
  Command,
  CommandResult,
  Acknowledgement,
  Rejection,
  Heartbeat,
  ConfigurationBegin,
  ConfigurationRecord,
  ConfigurationEnd,
  ConfigurationReport,
  BootstrapCommitted,
  DatastreamBound,
  OtaOffer,
  OtaReport,
  ScheduleRecord,
  ScheduleReport,
  ScheduleRenew,
  TimeRequest,
  TimeResponse,
  FlowControl
};

enum class FlovaLinkResultStatus : uint8_t { Ok = 0, Accepted = 1, Duplicate = 2, Error = 3 };
enum class FlovaLinkConfigurationPhase : uint8_t { Begin, Record, End };
enum class FlovaOtaStrategy : uint8_t { None = 0, Ab = 1, AbRecovery = 2 };
enum class FlovaOtaBootState : uint8_t { Stable = 0, Candidate = 1, RolledBack = 2 };
// Stable CBOR scalar discriminator. The transport encodes this as the first
// item of a fixed array; text is the only variable-size alternative and is
// bounded by the schema-derived storage below.
enum class FlovaLinkValueKind : uint8_t { Bool = 0, Int64 = 1, Float32 = 2, Float64 = 3, Text = 4 };

struct FlovaLinkId {
  uint8_t bytes[16];
  bool present;
};

struct FlovaLinkValue {
  FlovaLinkValueKind kind;
  union {
    bool boolean;
    int64_t integer;
    float float32;
    double float64;
    char text[FLOVA_LINK_TEXT_BYTES];
  } data;
};

struct FlovaLinkStateReading {
  DatastreamId datastreamId;
  FlovaLinkValue value;
  uint32_t revision;
};

struct FlovaLinkStateBatch {
  uint64_t messageId;
  uint32_t configurationGeneration;
  uint8_t count;
  FlovaLinkStateReading readings[FLOVA_LINK_MAX_STATE_READINGS];
};

struct FlovaLinkCommand {
  FlovaLinkId commandId;
  FlovaLinkId correlationId;
  uint32_t configurationGeneration;
  uint32_t desiredVersion;
  DatastreamId datastreamId;
  bool isUserCommand;
  // Zero means no expiry protection. A non-zero deadline requires a valid UTC
  // clock and is rejected before the application write handler is called.
  uint64_t expiresAtUtcMs;
  FlovaLinkValue value;
};

struct FlovaLinkCommandResult {
  uint64_t messageId;
  FlovaLinkId commandId;
  FlovaLinkId correlationId;
  uint32_t desiredVersion;
  DatastreamId datastreamId;
  FlovaLinkResultStatus status;
  bool duplicate;
  FlovaLinkValue value;
  char errorCode[FLOVA_LINK_TEXT_BYTES];
};

struct FlovaLinkAcknowledgement {
  uint64_t acknowledgedMessageId;
  uint32_t retryAfterMs;
  char reasonCode[FLOVA_LINK_TEXT_BYTES];
};

// CONFIG_* is intentionally a single fixed decoded record.  Implementations
// must persist/verify it before acknowledging it and may not assemble a full
// configuration document in RAM.
struct FlovaLinkConfigurationRecord {
  uint64_t messageId;
  uint32_t generation;
  uint32_t sequence;
  uint32_t recordCount;
  uint16_t schemaVersion;
  uint16_t maximumRecordBytes;
  uint8_t checksum[32];
  FlovaLinkConfigurationPhase phase;
  uint8_t recordType;
  DatastreamId datastreamId;
  char datastreamKey[FLOVA_LINK_TEXT_BYTES];
  uint16_t recordLength;
  uint8_t record[FLOVA_LINK_RECORD_BYTES];
  bool hasTypedUnit;
  flova::config::Unit typedUnit;
};

struct FlovaLinkConfigurationReport {
  uint64_t messageId;
  FlovaLinkId commandId;
  uint32_t generation;
  uint32_t sequence;
  uint8_t checksum[32];
  FlovaLinkResultStatus status;
  char errorCode[FLOVA_LINK_TEXT_BYTES];
};

// Post-boot proof that a complete, verified configuration generation was
// restored into the runtime. This is deliberately separate from CONFIG_ACK:
// an ACK only proves that one transfer frame was durably handled.
struct FlovaLinkConfigurationState {
  uint64_t messageId;
  uint32_t generation;
  uint8_t checksum[32];
  FlovaLinkResultStatus status;
};

struct FlovaLinkBootstrapCommitted {
  FlovaLinkId deviceId;
  uint32_t generation;
  uint64_t serverUtcMs;
};

struct FlovaLinkOtaOffer {
  uint64_t messageId;
  FlovaLinkId installId;
  FlovaLinkId releaseId;
  char version[FLOVA_LINK_OTA_VERSION_BYTES];
  char firmwareTarget[FLOVA_LINK_OTA_TARGET_BYTES];
  char url[FLOVA_LINK_OTA_URL_BYTES];
  char sha256[FLOVA_LINK_OTA_SHA256_BYTES];
  uint32_t sizeBytes;
};

struct FlovaOtaPendingRecord {
  uint32_t magic;
  FlovaLinkId installId;
  FlovaLinkId releaseId;
  char version[FLOVA_LINK_OTA_VERSION_BYTES];
};

struct FlovaLinkOtaReport {
  uint64_t messageId;
  FlovaLinkId installId;
  FlovaLinkResultStatus status;
  char errorCode[FLOVA_LINK_TEXT_BYTES];
};

struct FlovaLinkScheduleRecord {
  uint64_t messageId;
  uint32_t generation;
  uint32_t revision;
  uint16_t sequence;
  uint16_t recordLength;
  uint8_t record[FLOVA_LINK_RECORD_BYTES];
};

struct FlovaLinkScheduleStatus {
  uint64_t messageId;
  uint32_t generation;
  uint32_t revision;
  uint64_t validUntilUtcMs;
  FlovaLinkResultStatus status;
  char errorCode[FLOVA_LINK_TEXT_BYTES];
};

struct FlovaLinkHeartbeat {
  uint64_t messageId;
  uint32_t configurationGeneration;
  uint32_t uptimeMs;
  uint32_t capabilityMask;
  uint16_t protocolVersion;
  uint16_t datastreamSlots;
  uint16_t scheduleSlots;
  bool otaCapable;
  char firmwareVersion[FLOVA_LINK_OTA_VERSION_BYTES];
  char firmwareTarget[FLOVA_LINK_OTA_TARGET_BYTES];
  FlovaLinkId runningReleaseId;
  FlovaLinkId lastInstallId;
  uint32_t otaMaxImageBytes;
  FlovaOtaStrategy otaStrategy;
  bool otaRollbackCapable;
  char otaBootLayoutVersion[FLOVA_LINK_OTA_TARGET_BYTES];
  FlovaOtaBootState otaBootState;
  char otaRollbackReason[FLOVA_LINK_TEXT_BYTES];
};

struct FlovaLinkTimeRequest {
  uint64_t messageId;
  uint32_t monotonicMs;
};

struct FlovaLinkTimeResponse {
  uint64_t requestId;
  uint64_t serverUtcMs;
};

struct FlovaLinkInboundMessage {
  FlovaLinkMessageType type;
  uint64_t messageId;
  union {
    FlovaLinkCommand command;
    FlovaLinkAcknowledgement acknowledgement;
    FlovaLinkConfigurationRecord configuration;
    FlovaLinkBootstrapCommitted bootstrapCommitted;
    FlovaLinkOtaOffer otaOffer;
    FlovaLinkScheduleRecord schedule;
    FlovaLinkTimeResponse timeResponse;
    struct {
      uint32_t generation;
      uint8_t count;
      DatastreamId ids[FLOVA_MAX_ACTIVE_DATASTREAMS];
    } datastreamBound;
  } body;
};

typedef void (*FlovaMessageCallback)(const FlovaLinkInboundMessage& message);
typedef void (*FlovaMessageCallbackWithContext)(void* context, const FlovaLinkInboundMessage& message);
