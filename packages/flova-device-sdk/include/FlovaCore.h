#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "FlovaBuildConfig.h"
#include "FlovaResources.h"

namespace flova {

static const size_t kMaxDatastreams = FLOVA_DATASTREAM_CAPACITY;
static const size_t kMaxText = FLOVA_TEXT_CAPACITY;

enum class ValueType : uint8_t { Boolean, Float, Double, Text };
enum class Mode : uint8_t { State, Sample, Command, Event };
enum class OfflinePolicy : uint8_t { KeepLatest, StoreHistory, Drop, Reject };
enum class PersistencePolicy : uint8_t { None, Persistent };
enum class Origin : uint8_t { Unknown, LocalLogic, SensorRead, PhysicalInput, UserCommand, CloudAutomation, DeviceRestore, Internal };
enum class Quality : uint8_t { Good, Stale, Invalid, HardwareError };
enum class WriteStatus : uint8_t { Accepted, Rejected, NoChange, Failed };
enum class MessageKind : uint8_t { StateUpdate, WriteRequest, Acknowledgement, Error, Heartbeat, Configuration, TimeRequest, TimeResponse };

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

template <typename T> struct ReadResult {
  T value;
  const char* reason;
  bool ok;
  static ReadResult success(const T& value) { return {value, "", true}; }
  static ReadResult error(const char* reason) { return {T(), reason, false}; }
};

struct Value {
  ValueType type;
  union { bool boolean; float floating; double number; } scalar;
  char text[kMaxText];
  Value() : type(ValueType::Text) { scalar.number = 0; text[0] = 0; }
  static Value from(bool v) { Value out; out.type = ValueType::Boolean; out.scalar.boolean = v; return out; }
  static Value from(float v) { Value out; out.type = ValueType::Float; out.scalar.floating = v; return out; }
  static Value from(double v) { Value out; out.type = ValueType::Double; out.scalar.number = v; return out; }
  static Value from(const char* v) { Value out; out.type = ValueType::Text; copy(out.text, v); return out; }
  static void copy(char* target, const char* source) { if (!source) source = ""; strncpy(target, source, kMaxText - 1); target[kMaxText - 1] = 0; }
};

inline bool operator==(const Value& a, const Value& b) {
  if (a.type != b.type) return false;
  if (a.type == ValueType::Boolean) return a.scalar.boolean == b.scalar.boolean;
  if (a.type == ValueType::Float) return a.scalar.floating == b.scalar.floating;
  if (a.type == ValueType::Double) return a.scalar.number == b.scalar.number;
  return strcmp(a.text, b.text) == 0;
}

struct Message {
  MessageKind kind;
  char key[kMaxText];
  Value value;
  char commandId[kMaxText];
  char correlationId[kMaxText];
  char reason[kMaxText];
  uint32_t revision;
  uint64_t timestamp;
  uint64_t monotonic;
  Origin origin;
  Message() : kind(MessageKind::StateUpdate), revision(0), timestamp(0), monotonic(0), origin(Origin::Unknown) { key[0] = commandId[0] = correlationId[0] = reason[0] = 0; }
};

typedef void (*MessageReceiver)(void* context, const Message& message);

class Link {
 public:
  virtual ~Link() {}
  virtual bool begin() = 0;
  virtual bool connected() const = 0;
  virtual bool send(const Message& message) = 0;
  virtual void poll() = 0;
  virtual void setReceiver(MessageReceiver receiver, void* context) = 0;
};

class Storage {
 public:
  virtual ~Storage() {}
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
  uint32_t droppedHistory, expiredHistory, queueOverflow, storageFailures, duplicateCommands, clockSyncFailures;
  Diagnostics() : droppedHistory(0), expiredHistory(0), queueOverflow(0), storageFailures(0), duplicateCommands(0), clockSyncFailures(0) {}
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
  Device(Link& link, Storage& storage, Clock& clock, Logger& logger)
      : link_(link), storage_(storage), clock_(clock), logger_(logger), count_(0), recentCursor_(0), historyHead_(0), historyCount_(0), lastTimeRequest_(0), timeRequestStarted_(0), timeSequence_(0) {
    for (size_t i = 0; i < 4; ++i) recentCommands_[i][0] = 0;
    pendingTimeId_[0] = 0;
  }

  bool begin() {
    link_.setReceiver(receive, this);
    restore();
    restoreHistory();
    return link_.begin();
  }

  void run() {
    link_.poll();
    if (!link_.connected()) return;
    syncTime();
    for (size_t i = 0; i < count_; ++i) if (states_[i].dirty) publish(states_[i]);
    flushHistory();
  }

  const Diagnostics& diagnostics() const { return diagnostics_; }

  void resourcePlan(const ResourceBudget* budgets, size_t count) {
    resources_.configure(storage_.capabilities().usableBytes, budgets, count);
  }

  template <typename T> Datastream<T> datastream(const char* key);

 private:
  friend class Datastream<bool>;
  friend class Datastream<float>;
  friend class Datastream<double>;
  friend class Datastream<const char*>;

  struct State {
    char key[kMaxText];
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
    uint64_t updatedAt;
    uint64_t lastHistoryAt;
    HistoryRetentionPolicy history;
    WriteResult (*writeBool)(bool);
    WriteResult (*writeFloat)(float);
    WriteResult (*writeDouble)(double);
    WriteResult (*writeText)(const char*);
    ReadResult<bool> (*readBool)();
    ReadResult<float> (*readFloat)();
    ReadResult<double> (*readDouble)();
    ReadResult<const char*> (*readText)();
    State() : hasValue(false), mode(Mode::State), offline(OfflinePolicy::KeepLatest), persistence(PersistencePolicy::None), origin(Origin::Unknown), quality(Quality::Stale), dirty(false), revision(0), lastCloudRevision(0), updatedAt(0), lastHistoryAt(0), writeBool(0), writeFloat(0), writeDouble(0), writeText(0), readBool(0), readFloat(0), readDouble(0), readText(0) { key[0] = 0; }
  };
  struct Persisted { uint32_t magic; char key[kMaxText]; Value value; uint32_t revision; };
  struct HistoryRecord { char key[kMaxText]; Value value; uint64_t timestamp; uint64_t monotonic; uint64_t expiresAt; Origin origin; uint32_t revision; };
  struct HistoryMeta { uint32_t magic; uint16_t head; uint16_t count; };

  State* state(const char* key, ValueType type) {
    for (size_t i = 0; i < count_; ++i) if (strcmp(states_[i].key, key) == 0) return states_[i].value.type == type ? &states_[i] : 0;
    if (count_ == kMaxDatastreams || !key || strlen(key) >= kMaxText) return 0;
    State& out = states_[count_++]; Value::copy(out.key, key); out.value.type = type; return &out;
  }
  State* find(const char* key, ValueType type) {
    for (size_t i = 0; i < count_; ++i) if (strcmp(states_[i].key, key) == 0) return states_[i].value.type == type ? &states_[i] : 0;
    return 0;
  }

  static void receive(void* context, const Message& message) { static_cast<Device*>(context)->receive(message); }
  void receive(const Message& message) {
    if (message.kind == MessageKind::TimeResponse) return acceptTime(message);
    if (message.kind != MessageKind::WriteRequest) return;
    State* current = find(message.key, message.value.type);
    if (!current) return acknowledge(message, WriteResult::reject("unknown_datastream"), 0);
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
    if ((state.mode == Mode::Sample || state.mode == Mode::Event)) return WriteResult::reject("not_writable");
    if (!link_.connected() && state.offline == OfflinePolicy::Reject) return WriteResult::reject("offline_delivery_required");
    if (state.hasValue && state.value == value) return WriteResult::noChange();
    WriteResult result = invoke(state, value);
    if (result.accepted()) update(state, value, origin);
    return result;
  }

  WriteResult report(State& state, const Value& value, Origin origin) {
    if (!link_.connected() && state.offline == OfflinePolicy::Reject) return WriteResult::reject("offline_delivery_required");
    update(state, value, origin); return WriteResult::accept();
  }

  WriteResult invoke(State& s, const Value& v) {
    if (v.type == ValueType::Boolean && s.writeBool) return s.writeBool(v.scalar.boolean);
    if (v.type == ValueType::Float && s.writeFloat) return s.writeFloat(v.scalar.floating);
    if (v.type == ValueType::Double && s.writeDouble) return s.writeDouble(v.scalar.number);
    if (v.type == ValueType::Text && s.writeText) return s.writeText(v.text);
    return WriteResult::failure("write_handler_missing");
  }

  void setWrite(State* s, WriteResult (*h)(bool)) { if (s) s->writeBool = h; }
  void setWrite(State* s, WriteResult (*h)(float)) { if (s) s->writeFloat = h; }
  void setWrite(State* s, WriteResult (*h)(double)) { if (s) s->writeDouble = h; }
  void setWrite(State* s, WriteResult (*h)(const char*)) { if (s) s->writeText = h; }
  void setRead(State* s, ReadResult<bool> (*h)()) { if (s) s->readBool = h; }
  void setRead(State* s, ReadResult<float> (*h)()) { if (s) s->readFloat = h; }
  void setRead(State* s, ReadResult<double> (*h)()) { if (s) s->readDouble = h; }
  void setRead(State* s, ReadResult<const char*> (*h)()) { if (s) s->readText = h; }
  ReadResult<bool> read(State* s, bool*) { return s && s->readBool ? s->readBool() : ReadResult<bool>::error("read_handler_missing"); }
  ReadResult<float> read(State* s, float*) { return s && s->readFloat ? s->readFloat() : ReadResult<float>::error("read_handler_missing"); }
  ReadResult<double> read(State* s, double*) { return s && s->readDouble ? s->readDouble() : ReadResult<double>::error("read_handler_missing"); }
  ReadResult<const char*> read(State* s, const char**) { return s && s->readText ? s->readText() : ReadResult<const char*>::error("read_handler_missing"); }

  void update(State& state, const Value& value, Origin origin) {
    state.value = value; state.hasValue = true; state.origin = origin; state.quality = Quality::Good; state.revision++; state.updatedAt = clock_.milliseconds();
    state.dirty = !link_.connected() && state.offline == OfflinePolicy::KeepLatest;
    if (!link_.connected() && state.offline == OfflinePolicy::StoreHistory &&
        (!state.history.minimumIntervalMs || !state.lastHistoryAt ||
         clock_.milliseconds() - state.lastHistoryAt >= state.history.minimumIntervalMs)) {
      queueHistory(state);
      state.lastHistoryAt = clock_.milliseconds();
    }
    if (state.persistence == PersistencePolicy::Persistent) persist(state);
    if (link_.connected()) publish(state);
  }

  void publish(State& state) {
    Message message; message.kind = MessageKind::StateUpdate; Value::copy(message.key, state.key); message.value = state.value; message.revision = state.revision; message.origin = state.origin; message.timestamp = clock_.utcValid() ? clock_.utcMilliseconds() : 0; message.monotonic = clock_.milliseconds();
    if (link_.send(message)) state.dirty = false;
  }

  void acknowledge(const Message& request, const WriteResult& result, const State* state) {
    Message reply; reply.kind = result.accepted() ? MessageKind::Acknowledgement : MessageKind::Error; Value::copy(reply.key, request.key); Value::copy(reply.commandId, request.commandId); Value::copy(reply.correlationId, request.correlationId); Value::copy(reply.reason, result.reason); reply.revision = request.revision; if (state && state->hasValue) reply.value = state->value; link_.send(reply);
  }

  void persist(const State& state) {
    Persisted record; record.magic = 0x464C4F56UL; Value::copy(record.key, state.key); record.value = state.value; record.revision = state.revision;
    char key[kMaxText + 3]; snprintf(key, sizeof(key), "ds:%s", state.key); storage_.write(key, &record, sizeof(record));
  }

  void restore() {
    for (size_t i = 0; i < count_; ++i) if (states_[i].persistence == PersistencePolicy::Persistent) {
      char key[kMaxText + 3]; snprintf(key, sizeof(key), "ds:%s", states_[i].key); Persisted restored;
      if (storage_.read(key, &restored, sizeof(restored)) && restored.magic == 0x464C4F56UL && strcmp(restored.key, states_[i].key) == 0 && restored.value.type == states_[i].value.type) { states_[i].value = restored.value; states_[i].revision = restored.revision; states_[i].hasValue = true; states_[i].origin = Origin::DeviceRestore; states_[i].quality = Quality::Good; }
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
    Value::copy(record.key, state.key); record.value = state.value; record.timestamp = clock_.utcValid() ? clock_.utcMilliseconds() : 0; record.monotonic = clock_.milliseconds(); record.expiresAt = record.timestamp && state.history.maximumAgeSeconds ? record.timestamp + static_cast<uint64_t>(state.history.maximumAgeSeconds) * 1000 : 0; record.origin = state.origin; record.revision = state.revision; historyCount_++;
    char key[24]; snprintf(key, sizeof(key), "history:%u", (unsigned)slot);
    if (!storage_.write(key, &record, sizeof(record))) {
      historyCount_--; resources_.release(ResourceKind::History, recordBytes); diagnostics_.storageFailures++; return;
    }
    persistHistoryMeta();
  }
  void flushHistory() {
    expireHistory();
    while (historyCount_) { HistoryRecord& record = history_[historyHead_]; Message message; message.kind = MessageKind::StateUpdate; Value::copy(message.key, record.key); message.value = record.value; message.timestamp = record.timestamp; message.monotonic = record.monotonic; message.origin = record.origin; message.revision = record.revision; if (!link_.send(message)) return; dropOldest(false, false); }
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
    Message request; request.kind = MessageKind::TimeRequest; request.monotonic = now; snprintf(request.commandId, sizeof(request.commandId), "time-%lu", (unsigned long)++timeSequence_); if (link_.send(request)) { timeRequestStarted_ = now ? now : 1; lastTimeRequest_ = now; Value::copy(pendingTimeId_, request.commandId); }
  }
  void acceptTime(const Message& response) {
    if (!timeRequestStarted_ || !response.timestamp || strcmp(response.commandId, pendingTimeId_) != 0) return;
    uint64_t received = clock_.milliseconds(); uint64_t uncertainty = (received - timeRequestStarted_) / 2; clock_.setUtc(response.timestamp + uncertainty, uncertainty); timeRequestStarted_ = 0;
  }

  Link& link_; Storage& storage_; Clock& clock_; Logger& logger_; State states_[kMaxDatastreams]; size_t count_;
  char recentCommands_[4][kMaxText]; size_t recentCursor_;
  HistoryRecord history_[FLOVA_HISTORY_CAPACITY]; size_t historyHead_, historyCount_; ResourceManager resources_;
  uint64_t lastTimeRequest_, timeRequestStarted_; uint32_t timeSequence_; char pendingTimeId_[kMaxText]; Diagnostics diagnostics_;
};

template <typename T> struct Codec;
template <> struct Codec<bool> { static Value encode(bool v) { return Value::from(v); } static bool decode(const Value& v) { return v.scalar.boolean; } };
template <> struct Codec<float> { static Value encode(float v) { return Value::from(v); } static float decode(const Value& v) { return v.scalar.floating; } };
template <> struct Codec<double> { static Value encode(double v) { return Value::from(v); } static double decode(const Value& v) { return v.scalar.number; } };
template <> struct Codec<const char*> { static Value encode(const char* v) { return Value::from(v); } static const char* decode(const Value& v) { return v.text; } };

template <typename T> class Datastream {
 public:
  Datastream(Device& device, Device::State* state) : device_(device), state_(state) {}
  bool valid() const { return state_ != 0; }
  bool hasValue() const { return state_ && state_->hasValue; }
  T read() const { return hasValue() ? Codec<T>::decode(state_->value) : T(); }
  Snapshot<T> snapshot() const { Snapshot<T> out = {read(), hasValue(), 0, Origin::Unknown, Quality::Stale, false, 0}; if (state_) { out.updatedAt = state_->updatedAt; out.origin = state_->origin; out.quality = state_->quality; out.dirty = state_->dirty; out.revision = state_->revision; } return out; }
  WriteResult write(const T& value) { return state_ ? device_.apply(*state_, Codec<T>::encode(value), Origin::LocalLogic) : WriteResult::failure("registration_full"); }
  WriteResult report(const T& value, Origin origin = Origin::SensorRead) { return state_ ? device_.report(*state_, Codec<T>::encode(value), origin) : WriteResult::failure("registration_full"); }
  Datastream& mode(Mode value) { if (state_) state_->mode = value; return *this; }
  Datastream& offline(OfflinePolicy value) { if (state_) state_->offline = value; return *this; }
  Datastream& retention(const HistoryRetentionPolicy& value) { if (state_) state_->history = value; return *this; }
  Datastream& persist(PersistencePolicy value) { if (state_) state_->persistence = value; return *this; }
  Datastream& onWrite(WriteResult (*handler)(T)) { device_.setWrite(state_, handler); return *this; }
  Datastream& onRead(ReadResult<T> (*handler)()) { device_.setRead(state_, handler); return *this; }
  ReadResult<T> refresh() { ReadResult<T> result = device_.read(state_, static_cast<T*>(0)); if (result.ok) report(result.value, Origin::SensorRead); return result; }
 private: Device& device_; Device::State* state_;
};

template <> inline Datastream<bool> Device::datastream<bool>(const char* key) { return Datastream<bool>(*this, state(key, ValueType::Boolean)); }
template <> inline Datastream<float> Device::datastream<float>(const char* key) { return Datastream<float>(*this, state(key, ValueType::Float)); }
template <> inline Datastream<double> Device::datastream<double>(const char* key) { return Datastream<double>(*this, state(key, ValueType::Double)); }
template <> inline Datastream<const char*> Device::datastream<const char*>(const char* key) { return Datastream<const char*>(*this, state(key, ValueType::Text)); }

}  // namespace flova
