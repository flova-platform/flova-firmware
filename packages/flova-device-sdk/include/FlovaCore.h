#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "FlovaBuildConfig.h"
#include "FlovaConfigurationRuntime.h"
#include "FlovaDatastreamId.h"
#include "FlovaResources.h"

// Canonical standalone SDK runtime. This header intentionally owns only
// bounded domain state and service seams; board code supplies Link, Storage,
// Clock, and Logger implementations. Keep Arduino, ESP, GPIO, networking,
// filesystem, exceptions, RTTI, and unbounded containers out of this include
// closure so the same core can be host-tested and reused by other boards.
namespace flova {

static const size_t kMaxDatastreams = FLOVA_DATASTREAM_CAPACITY;
static const size_t kMaxText = FLOVA_TEXT_CAPACITY;

enum class ValueType : uint8_t { Boolean, Int64, Float, Double, Text };
enum class Mode : uint8_t { State, Sample, Command, Event };
enum class OfflinePolicy : uint8_t { KeepLatest, StoreHistory, Drop, Reject };
enum class PersistencePolicy : uint8_t { None, Persistent };
enum class Origin : uint8_t { Unknown, LocalLogic, SensorRead, PhysicalInput, UserCommand, CloudAutomation, DeviceRestore, Internal };
enum class Quality : uint8_t { Good, Stale, Invalid, HardwareError };
enum class WriteStatus : uint8_t { Accepted, Rejected, NoChange, Failed };
enum class MessageKind : uint8_t {
  StateUpdate,
  WriteRequest,
  Acknowledgement,
  Error,
  FlowControl,
  Rejection,
  Heartbeat,
  Configuration,
  TimeRequest,
  TimeResponse
};

struct WriteResult {
  WriteStatus status;
  const char* reason;
  const char* message;
  static WriteResult accept() { return {WriteStatus::Accepted, "", ""}; }
  static WriteResult reject(const char* reason, const char* message = "") { return {WriteStatus::Rejected, reason, message}; }
  static WriteResult noChange() { return {WriteStatus::NoChange, "", ""}; }
  static WriteResult failure(const char* reason, const char* message = "") { return {WriteStatus::Failed, reason, message}; }
  bool accepted() const { return status == WriteStatus::Accepted || status == WriteStatus::NoChange; }
};

inline WriteResult accept() { return WriteResult::accept(); }
inline WriteResult unchanged() { return WriteResult::noChange(); }
inline WriteResult reject(const char* reason, const char* message = "") {
  return WriteResult::reject(reason, message);
}

class Text {
 public:
  Text(const char* value = "") : valid_(copy(value)) {}
  const char* c_str() const { return value_; }
  bool empty() const { return value_[0] == 0; }
  bool valid() const { return valid_; }

 private:
  friend struct Value;
  bool copy(const char* value) {
    if (!value) value = "";
    const size_t length = strnlen(value, kMaxText);
    if (length >= kMaxText) {
      value_[0] = 0;
      return false;
    }
    memcpy(value_, value, length + 1);
    return true;
  }
  char value_[kMaxText];
  bool valid_;
};

struct Value {
  ValueType type;
  union { bool boolean; int64_t integer; float floating; double number; } scalar;
  char text[kMaxText];
  Value() : type(ValueType::Text) { scalar.number = 0; text[0] = 0; }
  static Value from(bool v) { Value out; out.type = ValueType::Boolean; out.scalar.boolean = v; return out; }
  static Value from(int64_t v) { Value out; out.type = ValueType::Int64; out.scalar.integer = v; return out; }
  static Value from(float v) { Value out; out.type = ValueType::Float; out.scalar.floating = v; return out; }
  static Value from(double v) { Value out; out.type = ValueType::Double; out.scalar.number = v; return out; }
  static Value from(const char* v) { Value out; out.type = ValueType::Text; copy(out.text, v); return out; }
  static Value from(const Text& v) { return from(v.c_str()); }
  static void copy(char* target, const char* source) { if (!source) source = ""; strncpy(target, source, kMaxText - 1); target[kMaxText - 1] = 0; }
};

inline bool operator==(const Value& a, const Value& b) {
  if (a.type != b.type) return false;
  if (a.type == ValueType::Boolean) return a.scalar.boolean == b.scalar.boolean;
  if (a.type == ValueType::Int64) return a.scalar.integer == b.scalar.integer;
  if (a.type == ValueType::Float) return a.scalar.floating == b.scalar.floating;
  if (a.type == ValueType::Double) return a.scalar.number == b.scalar.number;
  return strcmp(a.text, b.text) == 0;
}

struct Message {
  MessageKind kind;
  DatastreamId datastreamId;
  Value value;
  char commandId[kMaxText];
  char correlationId[kMaxText];
  char reason[kMaxText];
  uint32_t revision;
  uint64_t messageId;
  uint64_t timestamp;
  uint64_t monotonic;
  uint64_t expiresAtUtcMs;
  uint32_t retryAfterMs;
  Origin origin;
  Message()
      : kind(MessageKind::StateUpdate),
        datastreamId(FLOVA_INVALID_DATASTREAM_ID),
        revision(0),
        messageId(0),
        timestamp(0),
        monotonic(0),
        expiresAtUtcMs(0),
        retryAfterMs(0),
        origin(Origin::Unknown) {
    commandId[0] = correlationId[0] = reason[0] = 0;
  }
};

typedef void (*MessageReceiver)(void* context, const Message& message);

class Link {
 public:
  virtual ~Link() {}
  // Transport callbacks must queue bounded work and return. Device::run()
  // owns the point at which queued messages may invoke application hardware.
  virtual bool begin() = 0;
  virtual bool connected() const = 0;
  virtual bool send(const Message& message) = 0;
  virtual void poll() = 0;
  virtual void setReceiver(MessageReceiver receiver, void* context) = 0;
  virtual uint32_t messageNonce() const = 0;
  virtual bool bindDatastreams(const char* const*, size_t, DatastreamId*) { return false; }
  // Binding may complete after authentication. Adapters must keep the IDs
  // bounded and expose the completed result through readDatastreamBinding().
  virtual bool bindingReady() const { return true; }
  virtual bool readDatastreamBinding(DatastreamId*, size_t) const { return false; }
};

class Storage {
 public:
  virtual ~Storage() {}
  // The runtime owns service startup. Volatile or application-initialized
  // storage can keep the default; board storage overrides it.
  virtual bool begin() { return true; }
  virtual bool read(const char* key, void* output, size_t size) = 0;
  virtual bool write(const char* key, const void* value, size_t size) = 0;
  virtual bool remove(const char* key) = 0;
  virtual StorageCapabilities capabilities() const { return StorageCapabilities(); }
};

class Clock {
 public:
  virtual ~Clock() {}
  virtual uint64_t milliseconds() const = 0;
  virtual bool utcValid() const { return false; }
  virtual uint64_t utcMilliseconds() const { return 0; }
  virtual void setUtc(uint64_t, uint64_t) {}
};
class Logger { public: virtual ~Logger() {} virtual void log(const char* message) = 0; };

struct Diagnostics {
  uint32_t droppedHistory, expiredHistory, rejectedDeliveries, queueOverflow,
      storageFailures, duplicateCommands, clockSyncFailures;
  Diagnostics()
      : droppedHistory(0), expiredHistory(0), rejectedDeliveries(0),
        queueOverflow(0), storageFailures(0), duplicateCommands(0),
        clockSyncFailures(0) {}
};

template <typename T> struct Snapshot {
  T value;
  bool hasValue;
  uint64_t updatedAt;
  Origin origin;
  Quality quality;
  bool dirty;
  uint32_t revision;
};

class Device;
template <typename T> class Datastream;

class Device {
 public:
  typedef WriteResult (*ValueWriteHandler)(void* context, const Value& value);
  Device(Link& link, Storage& storage, Clock& clock, Logger& logger)
      : link_(link), storage_(storage), clock_(clock), logger_(logger), count_(0), recentCursor_(0), historyHead_(0), historyCount_(0), lastTimeRequest_(0), timeRequestStarted_(0), timeSequence_(0), nextMessageId_(static_cast<uint64_t>(link.messageNonce()) << 32), retryNotBefore_(0), historyLastSentAt_(0), started_(false), bindingPending_(false), bindingFailed_(false), resourcePlanConfigured_(false) {
    for (size_t i = 0; i < 4; ++i) recentCommands_[i][0] = 0;
    pendingTimeId_[0] = 0;
  }

  // Bind names once, restore bounded state, and start the board-supplied link.
  // A remote binding may complete after begin(); no hardware is touched until
  // run() observes a complete, validated binding.
  bool begin() {
    if (started_) return false;
    if (!link_.messageNonce()) return false;
    if (!resourcePlanConfigured_) configureDefaultResources();
    link_.setReceiver(receive, this);
    const char* keys[kMaxDatastreams] = {};
    DatastreamId ids[kMaxDatastreams] = {};
    for (size_t i = 0; i < count_; ++i) keys[i] = states_[i].key;
    if (count_ && !link_.bindDatastreams(keys, count_, ids)) return false;
    if (count_ && link_.bindingReady()) {
      if (!applyBinding(ids)) return false;
    } else if (count_) {
      bindingPending_ = true;
    }
    if (!bindingPending_) {
      restore();
      restoreHistory();
    }
    if (!link_.begin()) return false;
    started_ = true;
    return true;
  }

  // Call from the board loop. Hardware handlers and queued remote commands
  // are applied only through this lifecycle boundary.
  void run() {
    link_.poll();
    if (bindingPending_) {
      if (!link_.bindingReady()) return;
      DatastreamId ids[kMaxDatastreams] = {};
      if (!link_.readDatastreamBinding(ids, count_) || !applyBinding(ids)) {
        bindingFailed_ = true;
        bindingPending_ = false;
        return;
      }
      bindingPending_ = false;
      restore();
      restoreHistory();
    }
    if (!started_ || bindingFailed_ || !link_.connected()) return;
    if (retryNotBefore_ && clock_.milliseconds() < retryNotBefore_) return;
    syncTime();
    for (size_t i = 0; i < count_; ++i) if (states_[i].dirty) publish(states_[i]);
    flushHistory();
  }

  const Diagnostics& diagnostics() const { return diagnostics_; }
  size_t datastreamCount() const { return count_; }
  bool ready() const { return started_ && !bindingPending_ && !bindingFailed_ && link_.connected(); }
  uint64_t originateMessageId() { return ++nextMessageId_; }

  void resourcePlan(const ResourceBudget* budgets, size_t count) {
    resources_.configure(storage_.capabilities().usableBytes, budgets, count);
    resourcePlanConfigured_ = true;
  }

  template <typename T> Datastream<T> datastream(const char* key);

  bool setWriteHandler(DatastreamId id, ValueWriteHandler handler,
                       void* context) {
    State* current = stateForId(id);
    if (!current || !handler) return false;
    current->writeHandler.valueResult = handler;
    current->writeKind = WriteHandlerKind::ValueResult;
    current->writeContext = context;
    return true;
  }

  WriteResult write(DatastreamId id, const Value& value,
                    Origin origin = Origin::LocalLogic) {
    State* current = stateForId(id);
    return current ? apply(*current, value, origin)
                   : WriteResult::failure("unknown_datastream");
  }

  WriteResult report(DatastreamId id, const Value& value,
                     Origin origin = Origin::SensorRead) {
    State* current = stateForId(id);
    return current ? report(*current, value, origin)
                   : WriteResult::failure("unknown_datastream");
  }

  // Link adapters call this with one schema-decoded bounded unit. The public
  // datastream API remains typed and never exposes the wire representation.
  bool applyConfigurationUnit(const config::Unit& unit) {
    if (unit.kind == config::UnitKind::Datastream) {
      ValueType type;
      if (unit.data.datastream.valueType == 0) type = ValueType::Boolean;
      else if (unit.data.datastream.valueType == 1) type = ValueType::Int64;
      else if (unit.data.datastream.valueType == 2) type = ValueType::Float;
      else if (unit.data.datastream.valueType == 3) type = ValueType::Double;
      else if (unit.data.datastream.valueType == 4) type = ValueType::Text;
      else return false;
      if (!flovaValidDatastreamId(unit.data.datastream.id)) return false;
      State* current = nullptr;
      for (size_t i = 0; i < count_; ++i) if (states_[i].runtime.id == unit.data.datastream.id) { current = &states_[i]; break; }
      State* named = find(unit.data.datastream.key, type);
      if (current && named && current != named) return false;
      if (!current) current = named ? named : state(unit.data.datastream.key, type);
      if (!current) return false;
      current->runtime.id = unit.data.datastream.id;
      return true;
    }
    if (unit.kind == config::UnitKind::Safety) {
      State* current = 0;
      for (size_t i = 0; i < count_; ++i) if (states_[i].runtime.id == unit.data.safety.datastreamId) { current = &states_[i]; break; }
      if (!current) return false;
      current->safetyPolicy = static_cast<uint8_t>(unit.data.safety.policy);
      current->hasSafetyMinimum = unit.data.safety.hasMinimum && toValue(unit.data.safety.minimum, current->safetyMinimum);
      current->hasSafetyMaximum = unit.data.safety.hasMaximum && toValue(unit.data.safety.maximum, current->safetyMaximum);
      return current->safetyPolicy == 0 || current->safetyPolicy == 4 ||
             ((current->safetyPolicy == 1 || current->safetyPolicy == 2 || current->safetyPolicy == 3) &&
              (!unit.data.safety.hasMinimum || current->hasSafetyMinimum) &&
              (!unit.data.safety.hasMaximum || current->hasSafetyMaximum));
    }
    return true;
  }

  bool validateConfigurationUnit(const config::Unit& unit) {
    if (unit.kind == config::UnitKind::Datastream) {
      ValueType type;
      if (!configurationValueType(unit.data.datastream.valueType, type) ||
          !flovaValidDatastreamId(unit.data.datastream.id) ||
          !unit.data.datastream.key[0] ||
          strnlen(unit.data.datastream.key,
                  sizeof(unit.data.datastream.key)) >=
              sizeof(unit.data.datastream.key))
        return false;
      if ((unit.data.datastream.hasMinimum &&
           !config::valueTypeMatches(unit.data.datastream.minimum,
                                    unit.data.datastream.valueType)) ||
          (unit.data.datastream.hasMaximum &&
           !config::valueTypeMatches(unit.data.datastream.maximum,
                                    unit.data.datastream.valueType)) ||
          (unit.data.datastream.hasDefault &&
           !config::valueTypeMatches(unit.data.datastream.defaultValue,
                                    unit.data.datastream.valueType)))
        return false;
      for (size_t i = 0; i < count_; ++i)
        if (strcmp(states_[i].key, unit.data.datastream.key) == 0)
          return states_[i].value.type == type;
      return true;
    }
    if (unit.kind == config::UnitKind::Safety) {
      const uint8_t policy = static_cast<uint8_t>(unit.data.safety.policy);
      Value ignored;
      return flovaValidDatastreamId(unit.data.safety.datastreamId) &&
             policy <= 4 &&
             (!unit.data.safety.hasMinimum ||
              toValue(unit.data.safety.minimum, ignored)) &&
             (!unit.data.safety.hasMaximum ||
              toValue(unit.data.safety.maximum, ignored));
    }
    return static_cast<uint8_t>(unit.kind) <=
           static_cast<uint8_t>(config::UnitKind::ScheduleOccurrences);
  }

 private:
  template <typename T> friend class Datastream;

  enum class WriteHandlerKind : uint8_t {
    None,
    ValueResult,
    BoolResult, Int64Result, FloatResult, DoubleResult, TextResult,
    BoolResultContext, Int64ResultContext, FloatResultContext,
    DoubleResultContext, TextResultContext,
    BoolVoid, Int64Void, FloatVoid, DoubleVoid, TextVoid,
    BoolVoidContext, Int64VoidContext, FloatVoidContext,
    DoubleVoidContext, TextVoidContext
  };

  union WriteHandler {
    ValueWriteHandler valueResult;
    WriteResult (*boolResult)(bool);
    WriteResult (*int64Result)(int64_t);
    WriteResult (*floatResult)(float);
    WriteResult (*doubleResult)(double);
    WriteResult (*textResult)(Text);
    WriteResult (*boolResultContext)(void*, bool);
    WriteResult (*int64ResultContext)(void*, int64_t);
    WriteResult (*floatResultContext)(void*, float);
    WriteResult (*doubleResultContext)(void*, double);
    WriteResult (*textResultContext)(void*, Text);
    void (*boolVoid)(bool);
    void (*int64Void)(int64_t);
    void (*floatVoid)(float);
    void (*doubleVoid)(double);
    void (*textVoid)(Text);
    void (*boolVoidContext)(void*, bool);
    void (*int64VoidContext)(void*, int64_t);
    void (*floatVoidContext)(void*, float);
    void (*doubleVoidContext)(void*, double);
    void (*textVoidContext)(void*, Text);
    WriteHandler() : valueResult(0) {}
  };

  struct State {
    // Configuration decode buffers are reused for every record. Own the key so
    // a dynamically configured datastream never retains a pointer into that
    // transient workspace.
    char key[kMaxText];
    DatastreamRuntime runtime;
    Value value;
    bool hasValue;
    Mode mode;
    OfflinePolicy offline;
    PersistencePolicy persistence;
    Origin origin;
    Quality quality;
    bool dirty;
    uint32_t revision;
    uint32_t lastCloudRevision;
    uint64_t pendingMessageId;
    uint64_t pendingSentAt;
    uint8_t safetyPolicy;
    bool hasSafetyMinimum;
    bool hasSafetyMaximum;
    Value safetyMinimum;
    Value safetyMaximum;
    uint64_t updatedAt;
    uint64_t lastHistoryAt;
    HistoryRetentionPolicy history;
    void* writeContext;
    WriteHandlerKind writeKind;
    WriteHandler writeHandler;
    State() : runtime{FLOVA_INVALID_DATASTREAM_ID, 0, 0}, hasValue(false), mode(Mode::State), offline(OfflinePolicy::KeepLatest), persistence(PersistencePolicy::None), origin(Origin::Unknown), quality(Quality::Stale), dirty(false), revision(0), lastCloudRevision(0), pendingMessageId(0), pendingSentAt(0), safetyPolicy(0), hasSafetyMinimum(false), hasSafetyMaximum(false), updatedAt(0), lastHistoryAt(0), writeContext(0), writeKind(WriteHandlerKind::None), writeHandler() { key[0] = 0; }
  };
  struct Persisted { uint32_t magic; DatastreamId datastreamId; Value value; uint32_t revision; };
  struct HistoryRecord { DatastreamId datastreamId; Value value; uint64_t messageId; uint64_t timestamp; uint64_t monotonic; uint64_t expiresAt; Origin origin; uint32_t revision; };
  struct HistoryMeta { uint32_t magic; uint16_t head; uint16_t count; };

  void configureDefaultResources() {
    ResourceBudget budgets[static_cast<size_t>(ResourceKind::Count)];
    const StorageCapabilities storage = storage_.capabilities();
    const uint32_t historyCapacity =
        static_cast<uint32_t>(FLOVA_HISTORY_CAPACITY * sizeof(HistoryRecord));
    uint32_t historyMaximum = historyCapacity;
    if (storage.usableBytes < historyMaximum) historyMaximum = storage.usableBytes;
    if (storage.maxRecordBytes && storage.maxRecordBytes < sizeof(HistoryRecord))
      historyMaximum = 0;
    ResourceBudget& history =
        budgets[static_cast<size_t>(ResourceKind::History)];
    history.maximumBytes = historyMaximum;
    history.elastic = true;
    resources_.configure(storage.usableBytes, budgets,
                         static_cast<size_t>(ResourceKind::Count));
    resourcePlanConfigured_ = true;
  }

  State* state(const char* key, ValueType type) {
    if (!key || strlen(key) >= kMaxText) return 0;
    for (size_t i = 0; i < count_; ++i) if (strcmp(states_[i].key, key) == 0) return states_[i].value.type == type ? &states_[i] : 0;
    if (count_ == kMaxDatastreams) return 0;
    State& out = states_[count_++]; Value::copy(out.key, key); out.value.type = type; return &out;
  }
  State* find(const char* key, ValueType type) {
    if (!key) return 0;
    for (size_t i = 0; i < count_; ++i) if (strcmp(states_[i].key, key) == 0) return states_[i].value.type == type ? &states_[i] : 0;
    return 0;
  }
  State* stateForId(DatastreamId id) {
    for (size_t i = 0; i < count_; ++i)
      if (states_[i].runtime.id == id) return &states_[i];
    return 0;
  }

  static void receive(void* context, const Message& message) { static_cast<Device*>(context)->receive(message); }
  bool applyBinding(const DatastreamId* ids) {
    if (!ids) return false;
    for (size_t i = 0; i < count_; ++i) {
      if (!flovaValidDatastreamId(ids[i])) return false;
      for (size_t prior = 0; prior < i; ++prior) if (ids[prior] == ids[i]) return false;
    }
    for (size_t i = 0; i < count_; ++i) {
      states_[i].runtime.id = ids[i];
      if (states_[i].hasValue && states_[i].offline == OfflinePolicy::KeepLatest)
        states_[i].dirty = true;
    }
    return true;
  }
  void receive(const Message& message) {
    if (message.kind == MessageKind::TimeResponse) return acceptTime(message);
    if (message.kind == MessageKind::Acknowledgement) {
      acknowledgeDelivery(message.messageId);
      return;
    }
    if (message.kind == MessageKind::FlowControl) {
      retryNotBefore_ = clock_.milliseconds() + message.retryAfterMs;
      return;
    }
    if (message.kind == MessageKind::Rejection) {
      rejectDelivery(message.messageId);
      return;
    }
    if (message.kind != MessageKind::WriteRequest) return;
    State* current = 0;
    for (size_t i = 0; i < count_; ++i) if (states_[i].runtime.id == message.datastreamId) { current = &states_[i]; break; }
    if (!current) return acknowledge(message, WriteResult::reject("unknown_datastream"), 0);
    if (message.expiresAtUtcMs && (!clock_.utcValid() || clock_.utcMilliseconds() >= message.expiresAtUtcMs)) return acknowledge(message, WriteResult::reject(clock_.utcValid() ? "command_expired" : "utc_time_required"), current);
    if (seen(message.commandId)) { diagnostics_.duplicateCommands++; return acknowledge(message, WriteResult::noChange(), current); }
    bool stale = message.revision && message.revision <= current->lastCloudRevision;
    WriteResult result = stale ? WriteResult::noChange() : apply(*current, message.value, message.origin);
    if (result.accepted() && message.revision > current->lastCloudRevision) current->lastCloudRevision = message.revision;
    remember(message.commandId);
    acknowledge(message, result, current);
  }

  bool seen(const char* commandId) const {
    if (!commandId || !commandId[0]) return false;
    for (size_t i = 0; i < 4; ++i) if (strcmp(recentCommands_[i], commandId) == 0) return true;
    return false;
  }
  void remember(const char* commandId) {
    if (!commandId || !commandId[0] || seen(commandId)) return;
    Value::copy(recentCommands_[recentCursor_], commandId); recentCursor_ = (recentCursor_ + 1) % 4;
  }

  WriteResult apply(State& state, const Value& value, Origin origin) {
    if (state.value.type != value.type) return WriteResult::reject("type_mismatch");
    if ((state.mode == Mode::Sample || state.mode == Mode::Event)) return WriteResult::reject("not_writable");
    if (!safe(state, value)) return WriteResult::reject(state.safetyPolicy == 1 || state.safetyPolicy == 3 ? "safety_minimum" : "safety_maximum");
    if (!link_.connected() && state.offline == OfflinePolicy::Reject)
      return WriteResult::reject("offline_delivery_required");
    if (state.hasValue && state.value == value) return WriteResult::noChange();
    WriteResult result = invoke(state, value);
    // A rejected hardware write is authoritative: do not update cache,
    // persistence, revision, or outbound state until the handler accepts it.
    if (result.accepted()) update(state, value, origin);
    return result;
  }

  WriteResult report(State& state, const Value& value, Origin origin) {
    if (state.value.type != value.type) return WriteResult::reject("type_mismatch");
    if (!link_.connected() && state.offline == OfflinePolicy::Reject)
      return WriteResult::reject("offline_delivery_required");
    update(state, value, origin); return WriteResult::accept();
  }

  WriteResult invoke(State& s, const Value& v) {
    switch (s.writeKind) {
      case WriteHandlerKind::ValueResult: return s.writeHandler.valueResult(s.writeContext, v);
      case WriteHandlerKind::BoolResult: return s.writeHandler.boolResult(v.scalar.boolean);
      case WriteHandlerKind::Int64Result: return s.writeHandler.int64Result(v.scalar.integer);
      case WriteHandlerKind::FloatResult: return s.writeHandler.floatResult(v.scalar.floating);
      case WriteHandlerKind::DoubleResult: return s.writeHandler.doubleResult(v.scalar.number);
      case WriteHandlerKind::TextResult: return s.writeHandler.textResult(Text(v.text));
      case WriteHandlerKind::BoolResultContext: return s.writeHandler.boolResultContext(s.writeContext, v.scalar.boolean);
      case WriteHandlerKind::Int64ResultContext: return s.writeHandler.int64ResultContext(s.writeContext, v.scalar.integer);
      case WriteHandlerKind::FloatResultContext: return s.writeHandler.floatResultContext(s.writeContext, v.scalar.floating);
      case WriteHandlerKind::DoubleResultContext: return s.writeHandler.doubleResultContext(s.writeContext, v.scalar.number);
      case WriteHandlerKind::TextResultContext: return s.writeHandler.textResultContext(s.writeContext, Text(v.text));
      case WriteHandlerKind::BoolVoid: s.writeHandler.boolVoid(v.scalar.boolean); break;
      case WriteHandlerKind::Int64Void: s.writeHandler.int64Void(v.scalar.integer); break;
      case WriteHandlerKind::FloatVoid: s.writeHandler.floatVoid(v.scalar.floating); break;
      case WriteHandlerKind::DoubleVoid: s.writeHandler.doubleVoid(v.scalar.number); break;
      case WriteHandlerKind::TextVoid: s.writeHandler.textVoid(Text(v.text)); break;
      case WriteHandlerKind::BoolVoidContext: s.writeHandler.boolVoidContext(s.writeContext, v.scalar.boolean); break;
      case WriteHandlerKind::Int64VoidContext: s.writeHandler.int64VoidContext(s.writeContext, v.scalar.integer); break;
      case WriteHandlerKind::FloatVoidContext: s.writeHandler.floatVoidContext(s.writeContext, v.scalar.floating); break;
      case WriteHandlerKind::DoubleVoidContext: s.writeHandler.doubleVoidContext(s.writeContext, v.scalar.number); break;
      case WriteHandlerKind::TextVoidContext: s.writeHandler.textVoidContext(s.writeContext, Text(v.text)); break;
      default: return WriteResult::failure("write_handler_missing");
    }
    return WriteResult::accept();
  }

  static bool toValue(const config::Value& source, Value& target) {
    if (source.kind == config::ValueKind::Boolean) { target = Value::from(source.data.boolean); return true; }
    if (source.kind == config::ValueKind::Int64) { target = Value::from(source.data.integer); return true; }
    if (source.kind == config::ValueKind::Float32) { target = Value::from(source.data.float32); return true; }
    if (source.kind == config::ValueKind::Float64) { target = Value::from(source.data.float64); return true; }
    if (source.kind == config::ValueKind::Text) { target = Value::from(source.data.text); return true; }
    return false;
  }

  static bool configurationValueType(uint8_t input, ValueType& output) {
    if (input == 0) output = ValueType::Boolean;
    else if (input == 1) output = ValueType::Int64;
    else if (input == 2) output = ValueType::Float;
    else if (input == 3) output = ValueType::Double;
    else if (input == 4) output = ValueType::Text;
    else return false;
    return true;
  }

  static bool safe(const State& state, const Value& value) {
    if (!state.safetyPolicy || state.safetyPolicy == 4 || value.type == ValueType::Text) return true;
    if (value.type == ValueType::Boolean) return false;
    const double actual = numeric(value);
    if ((state.safetyPolicy == 1 || state.safetyPolicy == 3) && state.hasSafetyMinimum) {
      const double minimum = numeric(state.safetyMinimum);
      if (actual < minimum) return false;
    }
    if ((state.safetyPolicy == 2 || state.safetyPolicy == 3) && state.hasSafetyMaximum) {
      const double maximum = numeric(state.safetyMaximum);
      if (actual > maximum) return false;
    }
    return true;
  }

  static double numeric(const Value& value) {
    if (value.type == ValueType::Int64) return static_cast<double>(value.scalar.integer);
    return value.type == ValueType::Float ? value.scalar.floating : value.scalar.number;
  }

  void setWrite(State* s, WriteResult (*h)(bool)) { if (s) { s->writeHandler.boolResult = h; s->writeKind = WriteHandlerKind::BoolResult; } }
  void setWrite(State* s, WriteResult (*h)(int64_t)) { if (s) { s->writeHandler.int64Result = h; s->writeKind = WriteHandlerKind::Int64Result; } }
  void setWrite(State* s, WriteResult (*h)(float)) { if (s) { s->writeHandler.floatResult = h; s->writeKind = WriteHandlerKind::FloatResult; } }
  void setWrite(State* s, WriteResult (*h)(double)) { if (s) { s->writeHandler.doubleResult = h; s->writeKind = WriteHandlerKind::DoubleResult; } }
  void setWrite(State* s, WriteResult (*h)(Text)) { if (s) { s->writeHandler.textResult = h; s->writeKind = WriteHandlerKind::TextResult; } }
  void setWrite(State* s, WriteResult (*h)(void*, bool), void* c) { if (s) { s->writeHandler.boolResultContext = h; s->writeKind = WriteHandlerKind::BoolResultContext; s->writeContext = c; } }
  void setWrite(State* s, WriteResult (*h)(void*, int64_t), void* c) { if (s) { s->writeHandler.int64ResultContext = h; s->writeKind = WriteHandlerKind::Int64ResultContext; s->writeContext = c; } }
  void setWrite(State* s, WriteResult (*h)(void*, float), void* c) { if (s) { s->writeHandler.floatResultContext = h; s->writeKind = WriteHandlerKind::FloatResultContext; s->writeContext = c; } }
  void setWrite(State* s, WriteResult (*h)(void*, double), void* c) { if (s) { s->writeHandler.doubleResultContext = h; s->writeKind = WriteHandlerKind::DoubleResultContext; s->writeContext = c; } }
  void setWrite(State* s, WriteResult (*h)(void*, Text), void* c) { if (s) { s->writeHandler.textResultContext = h; s->writeKind = WriteHandlerKind::TextResultContext; s->writeContext = c; } }
  void setWrite(State* s, void (*h)(bool)) { if (s) { s->writeHandler.boolVoid = h; s->writeKind = WriteHandlerKind::BoolVoid; } }
  void setWrite(State* s, void (*h)(int64_t)) { if (s) { s->writeHandler.int64Void = h; s->writeKind = WriteHandlerKind::Int64Void; } }
  void setWrite(State* s, void (*h)(float)) { if (s) { s->writeHandler.floatVoid = h; s->writeKind = WriteHandlerKind::FloatVoid; } }
  void setWrite(State* s, void (*h)(double)) { if (s) { s->writeHandler.doubleVoid = h; s->writeKind = WriteHandlerKind::DoubleVoid; } }
  void setWrite(State* s, void (*h)(Text)) { if (s) { s->writeHandler.textVoid = h; s->writeKind = WriteHandlerKind::TextVoid; } }
  void setWrite(State* s, void (*h)(void*, bool), void* c) { if (s) { s->writeHandler.boolVoidContext = h; s->writeKind = WriteHandlerKind::BoolVoidContext; s->writeContext = c; } }
  void setWrite(State* s, void (*h)(void*, int64_t), void* c) { if (s) { s->writeHandler.int64VoidContext = h; s->writeKind = WriteHandlerKind::Int64VoidContext; s->writeContext = c; } }
  void setWrite(State* s, void (*h)(void*, float), void* c) { if (s) { s->writeHandler.floatVoidContext = h; s->writeKind = WriteHandlerKind::FloatVoidContext; s->writeContext = c; } }
  void setWrite(State* s, void (*h)(void*, double), void* c) { if (s) { s->writeHandler.doubleVoidContext = h; s->writeKind = WriteHandlerKind::DoubleVoidContext; s->writeContext = c; } }
  void setWrite(State* s, void (*h)(void*, Text), void* c) { if (s) { s->writeHandler.textVoidContext = h; s->writeKind = WriteHandlerKind::TextVoidContext; s->writeContext = c; } }

  void update(State& state, const Value& value, Origin origin) {
    state.value = value; state.hasValue = true; state.origin = origin; state.quality = Quality::Good; state.revision++; state.updatedAt = clock_.milliseconds();
    const bool bound = flovaValidDatastreamId(state.runtime.id);
    state.dirty = bound && (link_.connected() || state.offline == OfflinePolicy::KeepLatest);
    state.pendingMessageId = 0;
    state.pendingSentAt = 0;
    if (bound && !link_.connected() && state.offline == OfflinePolicy::StoreHistory &&
        (!state.history.minimumIntervalMs || !state.lastHistoryAt ||
         clock_.milliseconds() - state.lastHistoryAt >= state.history.minimumIntervalMs)) {
      queueHistory(state);
      state.lastHistoryAt = clock_.milliseconds();
    }
    if (state.persistence == PersistencePolicy::Persistent) persist(state);
    if (bound && link_.connected()) publish(state);
  }

  void publish(State& state) {
    if (!flovaValidDatastreamId(state.runtime.id)) return;
    const uint64_t now = clock_.milliseconds();
    if (state.pendingSentAt && now - state.pendingSentAt < 5000) return;
    if (!state.pendingMessageId) state.pendingMessageId = originateMessageId();
    Message message; message.kind = MessageKind::StateUpdate; message.messageId = state.pendingMessageId; message.datastreamId = state.runtime.id; message.value = state.value; message.revision = state.revision; message.origin = state.origin; message.timestamp = clock_.utcValid() ? clock_.utcMilliseconds() : 0; message.monotonic = now;
    if (link_.send(message)) state.pendingSentAt = now ? now : 1;
  }

  void acknowledge(const Message& request, const WriteResult& result, const State* state) {
    Message reply; reply.kind = result.accepted() ? MessageKind::Acknowledgement : MessageKind::Error; reply.messageId = originateMessageId(); reply.datastreamId = request.datastreamId; Value::copy(reply.commandId, request.commandId); Value::copy(reply.correlationId, request.correlationId); Value::copy(reply.reason, result.reason); reply.revision = request.revision; if (state && state->hasValue) reply.value = state->value; link_.send(reply);
  }

  void persist(const State& state) {
    if (!flovaValidDatastreamId(state.runtime.id)) return;
    Persisted record; record.magic = 0x464C4F56UL; record.datastreamId = state.runtime.id; record.value = state.value; record.revision = state.revision;
    char key[24]; snprintf(key, sizeof(key), "dsid:%u", static_cast<unsigned>(state.runtime.id)); storage_.write(key, &record, sizeof(record));
  }

  void restore() {
    for (size_t i = 0; i < count_; ++i) if (states_[i].persistence == PersistencePolicy::Persistent) {
      if (!flovaValidDatastreamId(states_[i].runtime.id)) continue;
      char key[24]; snprintf(key, sizeof(key), "dsid:%u", static_cast<unsigned>(states_[i].runtime.id)); Persisted restored;
      if (storage_.read(key, &restored, sizeof(restored)) && restored.magic == 0x464C4F56UL && restored.datastreamId == states_[i].runtime.id && restored.value.type == states_[i].value.type) { states_[i].value = restored.value; states_[i].revision = restored.revision; states_[i].hasValue = true; states_[i].origin = Origin::DeviceRestore; states_[i].quality = Quality::Good; }
    }
  }

  void queueHistory(const State& state) {
    expireHistory();
    const uint32_t recordBytes = sizeof(HistoryRecord);
    const uint32_t recordLimit = state.history.maximumRecords ? state.history.maximumRecords : FLOVA_HISTORY_CAPACITY;
    const uint32_t byteLimit = state.history.maximumBytes ? state.history.maximumBytes : resources_.budget(ResourceKind::History).maximumBytes;
    if (state.history.overflow == HistoryOverflow::DropNewest &&
        (historyCount_ >= recordLimit || resources_.usage(ResourceKind::History).usedBytes + recordBytes > byteLimit)) {
      diagnostics_.queueOverflow++; diagnostics_.droppedHistory++; return;
    }
    while (historyCount_ && (historyCount_ >= recordLimit || resources_.usage(ResourceKind::History).usedBytes + recordBytes > byteLimit)) dropOldest(false, true);
    if (historyCount_ >= FLOVA_HISTORY_CAPACITY || !resources_.reserve(ResourceKind::History, recordBytes)) {
      diagnostics_.queueOverflow++; diagnostics_.droppedHistory++; return;
    }
    size_t slot = (historyHead_ + historyCount_) % FLOVA_HISTORY_CAPACITY; HistoryRecord& record = history_[slot];
    record.datastreamId = state.runtime.id; record.value = state.value; record.messageId = originateMessageId(); record.timestamp = clock_.utcValid() ? clock_.utcMilliseconds() : 0; record.monotonic = clock_.milliseconds(); record.expiresAt = record.timestamp && state.history.maximumAgeSeconds ? record.timestamp + static_cast<uint64_t>(state.history.maximumAgeSeconds) * 1000 : 0; record.origin = state.origin; record.revision = state.revision; historyCount_++;
    char key[24]; snprintf(key, sizeof(key), "history:%u", (unsigned)slot);
    if (!storage_.write(key, &record, sizeof(record))) {
      historyCount_--; resources_.release(ResourceKind::History, recordBytes); diagnostics_.storageFailures++; return;
    }
    persistHistoryMeta();
  }
  void flushHistory() {
    expireHistory();
    if (!historyCount_) return;
    const uint64_t now = clock_.milliseconds();
    if (historyLastSentAt_ && now - historyLastSentAt_ < 5000) return;
    HistoryRecord& record = history_[historyHead_]; Message message; message.kind = MessageKind::StateUpdate; message.messageId = record.messageId; message.datastreamId = record.datastreamId; message.value = record.value; message.timestamp = record.timestamp; message.monotonic = record.monotonic; message.origin = record.origin; message.revision = record.revision; if (link_.send(message)) historyLastSentAt_ = now ? now : 1;
  }
  void dropOldest(bool expired, bool dropped) { char key[24]; snprintf(key, sizeof(key), "history:%u", (unsigned)historyHead_); storage_.remove(key); historyHead_ = (historyHead_ + 1) % FLOVA_HISTORY_CAPACITY; historyCount_--; resources_.release(ResourceKind::History, sizeof(HistoryRecord), expired || dropped); if (expired) diagnostics_.expiredHistory++; else if (dropped) diagnostics_.droppedHistory++; persistHistoryMeta(); }
  void expireHistory() { if (!clock_.utcValid()) return; const uint64_t now = clock_.utcMilliseconds(); while (historyCount_ && history_[historyHead_].expiresAt && history_[historyHead_].expiresAt <= now) dropOldest(true, false); }
  void persistHistoryMeta() { HistoryMeta meta = {0x48495354UL, (uint16_t)historyHead_, (uint16_t)historyCount_}; if (!storage_.write("history.meta", &meta, sizeof(meta))) diagnostics_.storageFailures++; }
  void restoreHistory() {
    HistoryMeta meta; if (!storage_.read("history.meta", &meta, sizeof(meta)) || meta.magic != 0x48495354UL || meta.head >= FLOVA_HISTORY_CAPACITY || meta.count > FLOVA_HISTORY_CAPACITY) return;
    historyHead_ = meta.head; historyCount_ = 0;
    for (size_t i = 0; i < meta.count; ++i) { size_t slot = (meta.head + i) % FLOVA_HISTORY_CAPACITY; char key[24]; snprintf(key, sizeof(key), "history:%u", (unsigned)slot); if (!storage_.read(key, &history_[slot], sizeof(HistoryRecord)) || !resources_.reserve(ResourceKind::History, sizeof(HistoryRecord))) { diagnostics_.storageFailures++; break; } historyCount_++; }
  }
  void syncTime() {
    uint64_t now = clock_.milliseconds();
    if (timeRequestStarted_ && now - timeRequestStarted_ > 30000) { timeRequestStarted_ = 0; diagnostics_.clockSyncFailures++; }
    if (timeRequestStarted_ || (clock_.utcValid() && now - lastTimeRequest_ < 21600000ULL)) return;
    Message request; request.kind = MessageKind::TimeRequest; request.messageId = originateMessageId(); request.monotonic = now; snprintf(request.commandId, sizeof(request.commandId), "time-%lu", (unsigned long)++timeSequence_); if (link_.send(request)) { timeRequestStarted_ = now ? now : 1; lastTimeRequest_ = now; Value::copy(pendingTimeId_, request.commandId); }
  }
  void acceptTime(const Message& response) {
    if (!timeRequestStarted_ || !response.timestamp || strcmp(response.commandId, pendingTimeId_) != 0) return;
    uint64_t received = clock_.milliseconds(); uint64_t uncertainty = (received - timeRequestStarted_) / 2; clock_.setUtc(response.timestamp + uncertainty, uncertainty); timeRequestStarted_ = 0;
  }

  void acknowledgeDelivery(uint64_t messageId) {
    if (!messageId) return;
    for (size_t i = 0; i < count_; ++i) {
      if (states_[i].pendingMessageId != messageId) continue;
      states_[i].pendingMessageId = 0;
      states_[i].pendingSentAt = 0;
      states_[i].dirty = false;
      return;
    }
    if (historyCount_ && history_[historyHead_].messageId == messageId) {
      dropOldest(false, false);
      historyLastSentAt_ = 0;
    }
  }

  void rejectDelivery(uint64_t messageId) {
    if (!messageId) return;
    for (size_t i = 0; i < count_; ++i) {
      if (states_[i].pendingMessageId != messageId) continue;
      states_[i].pendingMessageId = 0;
      states_[i].pendingSentAt = 0;
      states_[i].dirty = false;
      diagnostics_.rejectedDeliveries++;
      return;
    }
    if (historyCount_ && history_[historyHead_].messageId == messageId) {
      dropOldest(false, true);
      diagnostics_.rejectedDeliveries++;
      historyLastSentAt_ = 0;
    }
  }

  Link& link_; Storage& storage_; Clock& clock_; Logger& logger_; State states_[kMaxDatastreams]; size_t count_;
  char recentCommands_[4][kMaxText]; size_t recentCursor_;
  HistoryRecord history_[FLOVA_HISTORY_CAPACITY]; size_t historyHead_, historyCount_; ResourceManager resources_;
  uint64_t lastTimeRequest_, timeRequestStarted_; uint32_t timeSequence_; uint64_t nextMessageId_; uint64_t retryNotBefore_; uint64_t historyLastSentAt_; char pendingTimeId_[kMaxText]; Diagnostics diagnostics_;
  bool started_, bindingPending_, bindingFailed_, resourcePlanConfigured_;
};

template <typename T> struct Codec;
template <> struct Codec<bool> { static bool valid(bool) { return true; } static Value encode(bool v) { return Value::from(v); } static bool decode(const Value& v) { return v.scalar.boolean; } };
template <> struct Codec<int64_t> { static bool valid(int64_t) { return true; } static Value encode(int64_t v) { return Value::from(v); } static int64_t decode(const Value& v) { return v.scalar.integer; } };
template <> struct Codec<float> { static bool valid(float) { return true; } static Value encode(float v) { return Value::from(v); } static float decode(const Value& v) { return v.scalar.floating; } };
template <> struct Codec<double> { static bool valid(double) { return true; } static Value encode(double v) { return Value::from(v); } static double decode(const Value& v) { return v.scalar.number; } };
template <> struct Codec<Text> { static bool valid(const Text& v) { return v.valid(); } static Value encode(const Text& v) { return Value::from(v); } static Text decode(const Value& v) { return Text(v.text); } };

template <typename T> class Datastream {
 public:
  Datastream(Device& device, Device::State* state) : device_(device), state_(state) {}
  bool valid() const { return state_ != 0; }
  bool bound() const { return state_ && flovaValidDatastreamId(state_->runtime.id); }
  bool hasValue() const { return state_ && state_->hasValue; }
  T value() const { return hasValue() ? Codec<T>::decode(state_->value) : T(); }
  Snapshot<T> snapshot() const { Snapshot<T> out = {value(), hasValue(), 0, Origin::Unknown, Quality::Stale, false, 0}; if (state_) { out.updatedAt = state_->updatedAt; out.origin = state_->origin; out.quality = state_->quality; out.dirty = state_->dirty; out.revision = state_->revision; } return out; }
  WriteResult write(const T& value) { return !Codec<T>::valid(value) ? WriteResult::reject("text_too_long") : state_ ? device_.apply(*state_, Codec<T>::encode(value), Origin::LocalLogic) : WriteResult::failure("registration_full"); }
  WriteResult report(const T& value, Origin origin = Origin::SensorRead) { return !Codec<T>::valid(value) ? WriteResult::reject("text_too_long") : state_ ? device_.report(*state_, Codec<T>::encode(value), origin) : WriteResult::failure("registration_full"); }
  Datastream& mode(Mode value) { if (state_) state_->mode = value; return *this; }
  Datastream& offline(OfflinePolicy value) { if (state_) state_->offline = value; return *this; }
  Datastream& retention(const HistoryRetentionPolicy& value) { if (state_) state_->history = value; return *this; }
  Datastream& persist(PersistencePolicy value) { if (state_) state_->persistence = value; return *this; }
  Datastream& onWrite(WriteResult (*handler)(T)) { device_.setWrite(state_, handler); return *this; }
  Datastream& onWrite(WriteResult (*handler)(void*, T), void* context) { device_.setWrite(state_, handler, context); return *this; }
  Datastream& onWrite(void (*handler)(T)) { device_.setWrite(state_, handler); return *this; }
  Datastream& onWrite(void (*handler)(void*, T), void* context) { device_.setWrite(state_, handler, context); return *this; }
 private: Device& device_; Device::State* state_;
};

template <> inline Datastream<bool> Device::datastream<bool>(const char* key) { return Datastream<bool>(*this, state(key, ValueType::Boolean)); }
template <> inline Datastream<int64_t> Device::datastream<int64_t>(const char* key) { return Datastream<int64_t>(*this, state(key, ValueType::Int64)); }
template <> inline Datastream<float> Device::datastream<float>(const char* key) { return Datastream<float>(*this, state(key, ValueType::Float)); }
template <> inline Datastream<double> Device::datastream<double>(const char* key) { return Datastream<double>(*this, state(key, ValueType::Double)); }
template <> inline Datastream<Text> Device::datastream<Text>(const char* key) { return Datastream<Text>(*this, state(key, ValueType::Text)); }

}  // namespace flova
