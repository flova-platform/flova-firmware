#pragma once

#include <stdint.h>
#include <string.h>

#include "FlovaBuildConfig.h"
#include "FlovaDatastreamId.h"

// This is the shared, serialized-format-free configuration contract.  Link
// adapters fill exactly one Unit at a time; SDKs and board glue consume it
// without seeing CBOR or a generic object tree.
namespace flova {
namespace config {

static const size_t kConfigurationTextBytes = FLOVA_TEXT_CAPACITY;
static const size_t kConfigurationScheduleActions = 8;
static const size_t kConfigurationOccurrenceChunk = 16;

enum class ValueKind : uint8_t { Boolean = 0, Int64 = 1, Float32 = 2, Float64 = 3, Text = 4 };
enum class UnitKind : uint8_t { Datastream = 0, System = 1, Schedule = 2, Safety = 3, ScheduleOccurrences = 4 };
enum class MappingKind : uint8_t { DigitalInput = 0, DigitalOutput = 1, AnalogInput = 2, PwmOutput = 3 };
enum class SafetyPolicy : uint8_t { None = 0, Minimum = 1, Maximum = 2, Range = 3, CommandExpiry = 4 };

struct Value {
  ValueKind kind;
  union {
    bool boolean;
    int64_t integer;
    float float32;
    double float64;
    char text[kConfigurationTextBytes];
  } data;

};

struct HardwareMapping {
  MappingKind kind;
  uint16_t pin;
  bool hasActiveHigh;
  bool activeHigh;
  bool hasPull;
  uint8_t pull;
  bool hasDebounceMs;
  uint32_t debounceMs;
  bool hasSampleMs;
  uint32_t sampleMs;
  bool hasMinimumOutputMs;
  uint32_t minimumOutputMs;

};

struct Datastream {
  DatastreamId id;
  uint8_t valueType;
  char uuid[37];
  char key[kConfigurationTextBytes];
  bool hasMinimum;
  bool hasMaximum;
  bool hasDefault;
  bool hasMapping;
  Value minimum;
  Value maximum;
  Value defaultValue;
  HardwareMapping mapping;
};

struct System {
  bool hasHeartbeatMs;
  uint32_t heartbeatMs;
  bool hasStatusLedPin;
  uint8_t statusLedPin;
  bool hasStatusLedActiveLow;
  bool statusLedActiveLow;
  bool hasBatchFlushMs;
  uint32_t batchFlushMs;

};

struct ScheduleAction {
  uint32_t offsetMs;
  DatastreamId datastreamId;
  Value value;
};

struct Schedule {
  uint32_t id;
  bool enabled;
  uint64_t validFrom;
  uint64_t validUntil;
  uint8_t actionCount;
  ScheduleAction actions[kConfigurationScheduleActions];
};

struct ScheduleOccurrences {
  uint32_t scheduleId;
  uint16_t chunkIndex;
  uint16_t chunkCount;
  uint8_t occurrenceCount;
  uint64_t occurrences[kConfigurationOccurrenceChunk];
};

struct Safety {
  DatastreamId datastreamId;
  SafetyPolicy policy;
  bool hasMinimum;
  bool hasMaximum;
  bool hasTimeoutMs;
  Value minimum;
  Value maximum;
  uint32_t timeoutMs;
};

struct Unit {
  UnitKind kind;
  union {
    Datastream datastream;
    System system;
    Schedule schedule;
    ScheduleOccurrences occurrences;
    Safety safety;
  } data;
};

inline bool valueTypeMatches(const Value& value, uint8_t type) {
  return static_cast<uint8_t>(value.kind) == type ||
         (type == 2 && value.kind == ValueKind::Float32) ||
         (type == 3 && value.kind == ValueKind::Float64);
}

}  // namespace config
}  // namespace flova
