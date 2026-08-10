#include "FlovaDevice.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static FlovaDevice* activeDevice = nullptr;

#if FLOVA_DATASTREAM_LOGGING && defined(ARDUINO)
#define FLOVA_DATASTREAM_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define FLOVA_DATASTREAM_LOG(...) ((void)0)
#endif

static void dispatchMessage(const FlovaLinkInboundMessage& message) {
  if (activeDevice) activeDevice->handleMessage(message);
}

template <size_t N>
static bool copyText(char (&out)[N], const char* value) {
  if (!value) value = "";
  const size_t length = strlen(value);
  if (length >= N) return false;
  memcpy(out, value, length);
  out[length] = 0;
  return true;
}

static uint64_t hashText(const char* text) {
  uint64_t hash = 1469598103934665603ULL;
  for (; text && *text; ++text) {
    hash ^= static_cast<uint8_t>(*text);
    hash *= 1099511628211ULL;
  }
  return hash;
}

static uint64_t nextLinkId(uint32_t nonce, uint32_t sequence) {
  return (static_cast<uint64_t>(nonce) << 32) | sequence;
}

static bool linkIdPresent(const FlovaLinkId& id) { return id.present; }
static bool linkIdEqual(const FlovaLinkId& left, const FlovaLinkId& right) {
  return left.present == right.present && (!left.present || memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0);
}
static uint64_t linkIdHash(const FlovaLinkId& id) {
  uint64_t hash = 1469598103934665603ULL;
  if (!id.present) return 0;
  for (size_t i = 0; i < sizeof(id.bytes); ++i) { hash ^= id.bytes[i]; hash *= 1099511628211ULL; }
  return hash;
}
static void linkIdHex(const FlovaLinkId& id, char out[33]) {
  static const char hex[] = "0123456789abcdef";
  if (!id.present) { out[0] = 0; return; }
  for (size_t i = 0; i < sizeof(id.bytes); ++i) { out[i * 2] = hex[id.bytes[i] >> 4]; out[i * 2 + 1] = hex[id.bytes[i] & 15]; }
  out[32] = 0;
}

static FlovaLinkResultStatus linkStatus(const FlovaWriteResult& result) {
  return result.accepted() ? FlovaLinkResultStatus::Ok : FlovaLinkResultStatus::Error;
}

static const char* valueTypeName(FlovaValueType type) {
  switch (type) {
    case FlovaValueType::Bool: return "bool";
    case FlovaValueType::Float: return "float";
    case FlovaValueType::Number: return "number";
    case FlovaValueType::String: return "string";
  }
  return "unknown";
}

static const char* valueForDatastreamLog(const String& value, FlovaValueType type) {
  if (type != FlovaValueType::Bool) return "<redacted>";
  return value == "true" || value == "1" ? "true" : "false";
}

static const char* mappingKindName(flova::config::MappingKind kind) {
  switch (kind) {
    case flova::config::MappingKind::DigitalInput: return "digital_input";
    case flova::config::MappingKind::DigitalOutput: return "digital_output";
    case flova::config::MappingKind::AnalogInput: return "analog_input";
    case flova::config::MappingKind::PwmOutput: return "pwm_output";
  }
  return "unknown";
}

static bool linkValueFromString(const String& text, FlovaValueType type, FlovaLinkValue& out) {
  memset(&out, 0, sizeof(out));
  switch (type) {
    case FlovaValueType::Bool:
      out.kind = FlovaLinkValueKind::Bool;
      out.data.boolean = text == "true" || text == "1";
      return text == "true" || text == "false" || text == "1" || text == "0";
    case FlovaValueType::Float:
      out.kind = FlovaLinkValueKind::Float32;
      out.data.float32 = text.toFloat();
      return true;
    case FlovaValueType::Number:
      out.kind = FlovaLinkValueKind::Float64;
      out.data.float64 = text.toDouble();
      return true;
    case FlovaValueType::String:
      out.kind = FlovaLinkValueKind::Text;
      return copyText(out.data.text, text.c_str());
  }
  return false;
}

static bool stringFromLinkValue(const FlovaLinkValue& value, String& out) {
  switch (value.kind) {
    case FlovaLinkValueKind::Bool: out = value.data.boolean ? "true" : "false"; return true;
    case FlovaLinkValueKind::Int64: out = String(static_cast<long long>(value.data.integer)); return true;
    case FlovaLinkValueKind::Float32: out = String(value.data.float32, 6); return true;
    case FlovaLinkValueKind::Float64: out = String(value.data.float64, 12); return true;
    case FlovaLinkValueKind::Text:
      if (memchr(value.data.text, 0, FLOVA_LINK_TEXT_BYTES) == nullptr) return false;
      out = value.data.text;
      return true;
  }
  return false;
}

void FlovaDevice::configure(const FlovaConfig& config) { config_ = config; initializeResourceContract(); }

void FlovaDevice::setActiveConfiguration(uint32_t generation, const uint8_t checksum[32],
                                         bool valid) {
  configurationGeneration_ = generation;
  configurationValid_ = valid && generation != 0;
  if (checksum) memcpy(configurationChecksum_, checksum, sizeof(configurationChecksum_));
  else memset(configurationChecksum_, 0, sizeof(configurationChecksum_));
  transport_.setConfigurationGeneration(generation);
}

void FlovaDevice::initializeResourceContract() {
  config_.capabilities.datastreamSlots = sizeof(states_) / sizeof(states_[0]);
  config_.capabilities.hardwareInputSlots = sizeof(inputs_) / sizeof(inputs_[0]);
  config_.capabilities.hardwareOutputSlots = sizeof(outputs_) / sizeof(outputs_[0]);
  config_.capabilities.commandDedupSlots = sizeof(recentCommandIds_) / sizeof(recentCommandIds_[0]);
  config_.capabilities.scheduleSlots = FLOVA_SCHEDULE_RUNTIME_ENABLED ? FLOVA_SCHEDULE_CAPACITY : 0;
  if (!FLOVA_SCHEDULE_RUNTIME_ENABLED) config_.capabilities.scheduleManifestBytes = 0;
  if (!FLOVA_HISTORY_RUNTIME_ENABLED) config_.capabilities.historyBytes = 0;
  if (!config_.capabilities.messageBytes) config_.capabilities.messageBytes = 512;
  if (config_.capabilities.messageBytes > 512) config_.capabilities.messageBytes = 512;
  if (!config_.limits.datastreams || config_.limits.datastreams > config_.capabilities.datastreamSlots) config_.limits.datastreams = config_.capabilities.datastreamSlots;
  if (!config_.limits.hardwareInputs || config_.limits.hardwareInputs > config_.capabilities.hardwareInputSlots) config_.limits.hardwareInputs = config_.capabilities.hardwareInputSlots;
  if (!config_.limits.hardwareOutputs || config_.limits.hardwareOutputs > config_.capabilities.hardwareOutputSlots) config_.limits.hardwareOutputs = config_.capabilities.hardwareOutputSlots;
  if (!config_.limits.commandDedup || config_.limits.commandDedup > config_.capabilities.commandDedupSlots) config_.limits.commandDedup = config_.capabilities.commandDedupSlots;
  if (!config_.limits.messageBytes || config_.limits.messageBytes > config_.capabilities.messageBytes) config_.limits.messageBytes = config_.capabilities.messageBytes;
}

bool FlovaDevice::begin() {
  return beginTransportOnly() && reconnect();
}

bool FlovaDevice::beginTransportOnly() {
  activeDevice = this;
  bootNonce_ = static_cast<uint32_t>(random(1, 0x7fffffffL));
  transport_.setCallback(dispatchMessage);
  // A fixed binary/hex implementation belongs to the storage adapter. The
  // old comma-delimited command string is intentionally not migrated.
  if (!configurationRuntimeDeferred_) {
    onRuntimeRestoreBegin();
    restorePersistentStates();
    restoreScheduleManifest();
    const bool restored = restoreActiveConfiguration();
    onRuntimeRestoreComplete(restored);
    if (!restored) return false;
  }
  return transport_.begin();
}

void FlovaDevice::loop() {
  transport_.loop();
  applyPendingConfiguration();
  processPendingOta();
  if (!transport_.connected()) reconnect();
  else if (connectedSinceMs_ && clock_.millisNow() - connectedSinceMs_ >= 300000UL) {
    reconnectDelayMs_ = 1000;
    reconnectBackoffCeilingMs_ = 1000;
  }
  updateCandidateHealth();
  processFactoryResetGesture();
  updateStatusIndicator();
  const uint32_t now = clock_.millisNow();
  if (transport_.connected() && now - lastHeartbeatMs_ >= config_.heartbeatIntervalMs) publishHeartbeat();
  if (transport_.connected() && (!clock_.utcValid() || now - lastTimeRequestMs_ >= 21600000UL)) requestTimeSync();
  if (transport_.connected()) { pollDigitalInputs(); pollAnalogInputs(); }
  flushDigitalOutputs();
  if (transport_.connected()) { flushDueStates(); flushDirtyStates(); }
  runSchedules();
}

void FlovaDevice::updateCandidateHealth() {
  if (!bootControl_ || bootControl_->state() != FlovaBootState::Candidate) return;
  const uint32_t now = clock_.millisNow();
  if (!candidateStartedMs_) candidateStartedMs_ = now ? now : 1;
  if (transport_.connected() && candidateHeartbeatPublished_) {
    if (!candidateHealthySinceMs_) candidateHealthySinceMs_ = now ? now : 1;
    if (now - candidateHealthySinceMs_ >= 30000UL) { if (bootControl_->confirmCandidate()) publishHeartbeat(); return; }
  } else candidateHealthySinceMs_ = 0;
  if (now - candidateStartedMs_ >= 120000UL) {
    reportOta(FlovaLinkResultStatus::Error, "health_timeout");
    bootControl_->rollbackCandidate();
  }
}

void FlovaDevice::addDigitalOutput(const char* key, uint8_t pin, bool activeHigh, uint32_t minOutputIntervalMs) {
  if (outputCount_ + pwmOutputCount_ >= config_.limits.hardwareOutputs) return;
  outputs_[outputCount_].key = String(key); outputs_[outputCount_].pin = pin;
  outputs_[outputCount_].activeHigh = activeHigh; outputs_[outputCount_].minOutputIntervalMs = minOutputIntervalMs; outputCount_++;
  DatastreamState* state = stateFor(key, FlovaValueType::Bool, true);
  if (state) { state->type = FlovaValueType::Bool; state->value = "false"; state->hasValue = true; state->quality = FlovaValueQuality::Good; }
  pinMode(pin, OUTPUT); digitalWrite(pin, activeHigh ? LOW : HIGH);
}

void FlovaDevice::addDigitalInput(const char* key, uint8_t pin, bool activeHigh, uint32_t debounceMs, uint8_t mode) {
  if (inputCount_ + analogInputCount_ >= config_.limits.hardwareInputs) {
    FLOVA_DATASTREAM_LOG("[flova] datastream input rejected key=%s pin=%u reason=hardware_input_capacity\n", key ? key : "", pin);
    return;
  }
  pinMode(pin, mode); const bool raw = digitalRead(pin) == (activeHigh ? HIGH : LOW);
  inputs_[inputCount_].key = String(key); inputs_[inputCount_].pin = pin; inputs_[inputCount_].activeHigh = activeHigh;
  inputs_[inputCount_].debounceMs = debounceMs; inputs_[inputCount_].lastRaw = raw; inputs_[inputCount_].lastSent = !raw;
  inputs_[inputCount_++].changedAt = clock_.millisNow();
  FLOVA_DATASTREAM_LOG("[flova] datastream input configured key=%s pin=%u active_high=%u pull=%s debounce_ms=%lu initial=%s\n",
                       key ? key : "", pin, activeHigh ? 1 : 0, mode == INPUT_PULLUP ? "up" : "none",
                       static_cast<unsigned long>(debounceMs), raw ? "true" : "false");
}

void FlovaDevice::addAnalogInput(const char* key, uint8_t pin, uint32_t sampleIntervalMs) {
  if (inputCount_ + analogInputCount_ >= config_.limits.hardwareInputs) return;
  AnalogInput& input = analogInputs_[analogInputCount_++]; input.key = String(key); input.pin = pin;
  input.sampleIntervalMs = sampleIntervalMs < 100 ? 100 : sampleIntervalMs;
  DatastreamState* state = stateFor(key, FlovaValueType::Number, true);
  if (state) { state->type = FlovaValueType::Number; state->mode = FlovaDatastreamMode::Sample; state->offline = FlovaOfflinePolicy::Drop; }
  pinMode(pin, INPUT);
}

void FlovaDevice::addPwmOutput(const char* key, uint8_t pin, double minimum, double maximum, double initialValue) {
  if (outputCount_ + pwmOutputCount_ >= config_.limits.hardwareOutputs || minimum >= maximum) return;
  PwmOutput& output = pwmOutputs_[pwmOutputCount_++]; output.key = String(key); output.pin = pin; output.minimum = minimum; output.maximum = maximum;
  DatastreamState* state = stateFor(key, FlovaValueType::Number, true);
  if (state) { state->type = FlovaValueType::Number; state->mode = FlovaDatastreamMode::State; state->value = String(initialValue); state->hasValue = true; state->quality = FlovaValueQuality::Good; }
  pinMode(pin, OUTPUT); applyPwmOutput(output, String(initialValue));
}

FlovaDevice::DatastreamState* FlovaDevice::stateFor(const char* key, FlovaValueType type, bool create) {
  for (uint8_t i = 0; i < stateCount_; ++i) if (states_[i].key == key) return &states_[i];
  if (!create || stateCount_ >= config_.limits.datastreams) return nullptr;
  states_[stateCount_].key = key; states_[stateCount_].type = type; return &states_[stateCount_++];
}

const FlovaDevice::DatastreamState* FlovaDevice::stateFor(const char* key) const {
  for (uint8_t i = 0; i < stateCount_; ++i) if (states_[i].key == key) return &states_[i];
  return nullptr;
}

bool FlovaDevice::valueMatchesType(const String& value, FlovaValueType type) const {
  if (type == FlovaValueType::Bool) return value == "true" || value == "false" || value == "1" || value == "0";
  if (type == FlovaValueType::String) return value.length() < FLOVA_TEXT_CAPACITY;
  if (!value.length()) return false;
  bool decimal = false;
  for (uint16_t i = 0; i < value.length(); ++i) { const char c = value[i]; if (c == '-' && i == 0) continue; if (c == '.' && !decimal) { decimal = true; continue; } if (c < '0' || c > '9') return false; }
  return true;
}

void FlovaDevice::updateState(DatastreamState& state, const String& value, FlovaValueOrigin origin, bool dirty) {
  state.value = value; state.hasValue = true; state.origin = origin; state.quality = FlovaValueQuality::Good;
  state.updatedAt = clock_.millisNow(); state.revision++; state.dirty = dirty;
  if (state.persistence == FlovaPersistencePolicy::Persistent) persistState(state);
}

void FlovaDevice::persistState(const DatastreamState& state) {
  char record[FLOVA_TEXT_CAPACITY * 2 + 32] = {};
  snprintf(record, sizeof(record), "%u|%s|%lu|%s", static_cast<unsigned>(state.type), state.key.c_str(), static_cast<unsigned long>(state.revision), state.value.c_str());
  storage_.setString(("ds:" + state.key).c_str(), record);
}

void FlovaDevice::restorePersistentStates() {
  for (uint8_t i = 0; i < stateCount_; ++i) {
    if (states_[i].persistence != FlovaPersistencePolicy::Persistent) continue;
    char stored[FLOVA_TEXT_CAPACITY * 2 + 32] = {};
    if (!storage_.getString(("ds:" + states_[i].key).c_str(), stored, sizeof(stored))) continue;
    String row(stored); const int first = row.indexOf('|'), second = row.indexOf('|', first + 1), third = row.indexOf('|', second + 1);
    if (first < 1 || second <= first || third <= second || row.substring(first + 1, second) != states_[i].key) continue;
    const FlovaValueType type = static_cast<FlovaValueType>(row.substring(0, first).toInt()); const String value = row.substring(third + 1);
    if (type == states_[i].type && valueMatchesType(value, type)) { states_[i].value = value; states_[i].hasValue = true; states_[i].revision = row.substring(second + 1, third).toInt(); states_[i].origin = FlovaValueOrigin::DeviceRestore; states_[i].quality = FlovaValueQuality::Good; states_[i].dirty = true; }
  }
}

bool FlovaDevice::readCached(const char* key, String& value, uint32_t* revision) const { const DatastreamState* state = stateFor(key); if (!state || !state->hasValue) return false; value = state->value; if (revision) *revision = state->revision; return true; }
bool FlovaDevice::readSnapshotMetadata(const char* key, uint32_t& updatedAt, FlovaValueOrigin& origin, FlovaValueQuality& quality, bool& dirty, uint32_t& revision) const { const DatastreamState* state = stateFor(key); if (!state) return false; updatedAt = state->updatedAt; origin = state->origin; quality = state->quality; dirty = state->dirty; revision = state->revision; return state->hasValue; }
bool FlovaDevice::hasValue(const char* key) const { String ignored; return readCached(key, ignored); }
bool FlovaDevice::registerTypedWrite(const char* key, void* handler, FlovaValueType type) { DatastreamState* state = stateFor(key, type, true); if (!state) return false; state->type = type; state->writeHandler = handler; return true; }
bool FlovaDevice::registerTypedRead(const char* key, void* handler, FlovaValueType type) { DatastreamState* state = stateFor(key, type, true); if (!state) return false; state->type = type; state->readHandler = handler; return true; }
void FlovaDevice::configureDatastream(const char* key, FlovaDatastreamMode mode, FlovaOfflinePolicy offline, FlovaPersistencePolicy persistence, FlovaRestorePolicy restore) { DatastreamState* state = stateFor(key, FlovaValueType::String, true); if (state) { state->mode = mode; state->offline = offline; state->persistence = persistence; state->restore = restore; } }
void FlovaDevice::configurePublishInterval(const char* key, uint32_t minimumIntervalMs) { DatastreamState* state = stateFor(key, FlovaValueType::String, true); if (state) state->minimumPublishIntervalMs = minimumIntervalMs; }
void FlovaDevice::enableStateBatching(uint8_t maximumReadings, uint32_t flushIntervalMs) { batchMaximumReadings_ = min<uint8_t>(maximumReadings, FLOVA_LINK_MAX_STATE_READINGS); batchFlushIntervalMs_ = flushIntervalMs ? flushIntervalMs : 1; }

FlovaWriteResult FlovaDevice::invokeWriteHandler(DatastreamState& state, const String& value, const FlovaLinkId&, const FlovaLinkId&) {
  if (state.writeHandler) {
    if (state.type == FlovaValueType::Bool) return reinterpret_cast<FlovaWriteResult (*)(bool)>(state.writeHandler)(value == "true" || value == "1");
    if (state.type == FlovaValueType::Float) return reinterpret_cast<FlovaWriteResult (*)(float)>(state.writeHandler)(value.toFloat());
    if (state.type == FlovaValueType::Number) return reinterpret_cast<FlovaWriteResult (*)(double)>(state.writeHandler)(value.toDouble());
    return reinterpret_cast<FlovaWriteResult (*)(String)>(state.writeHandler)(value);
  }
  for (uint8_t i = 0; i < outputCount_; ++i) if (outputs_[i].key == state.key) { applyDigitalOutput(outputs_[i], value == "true" || value == "1"); return FlovaWriteResult::accept(); }
  for (uint8_t i = 0; i < pwmOutputCount_; ++i) if (pwmOutputs_[i].key == state.key) return applyPwmOutput(pwmOutputs_[i], value);
  return FlovaWriteResult::failure("write_handler_missing");
}

bool FlovaDevice::publishState(DatastreamState& state) {
  FlovaLinkStateBatch message = {}; message.messageId = stateMessageId(state); message.configurationGeneration = configurationGeneration_; message.count = 1;
  if (!transport_.datastreamIdForKey(state.key.c_str(), message.readings[0].datastreamId)) {
    FLOVA_DATASTREAM_LOG("[flova] datastream publish key=%s accepted=0 reason=compact_id_missing\n", state.key.c_str());
    return false;
  }
  if (!linkValueFromString(state.value, state.type, message.readings[0].value)) {
    FLOVA_DATASTREAM_LOG("[flova] datastream publish key=%s accepted=0 reason=encoding_failed\n", state.key.c_str());
    return false;
  }
  message.readings[0].revision = state.revision;
  const bool published = transport_.publishState(message);
  if (published) { state.lastPublishedMs = clock_.millisNow(); state.publishPending = false; }
  FLOVA_DATASTREAM_LOG("[flova] datastream publish key=%s id=%u revision=%lu accepted=%u\n",
                       state.key.c_str(), message.readings[0].datastreamId,
                       static_cast<unsigned long>(state.revision), published ? 1 : 0);
  return published;
}

bool FlovaDevice::publishStateBatch() {
  if (!batchMaximumReadings_ || batchReadingCount_) return false;
  uint8_t count = 0;
  for (uint8_t i = 0; i < stateCount_ && count < batchMaximumReadings_; ++i) {
    DatastreamState& state = states_[i];
    if (!state.publishPending || state.mode == FlovaDatastreamMode::Event || state.value.length() >= FLOVA_TEXT_CAPACITY) continue;
    batchReadings_[count].stateIndex = i; batchReadings_[count].revision = state.revision; state.value.toCharArray(batchReadings_[count].value, FLOVA_TEXT_CAPACITY);
    state.batchedRevision = state.revision; state.batchInFlight = true; state.publishPending = false; ++count;
  }
  if (!count) return false;
  batchMessageId_ = nextLinkId(bootNonce_, ++batchSequence_); batchReadingCount_ = count;
  FlovaLinkStateBatch message = {}; message.messageId = batchMessageId_; message.configurationGeneration = configurationGeneration_; message.count = count;
  for (uint8_t i = 0; i < count; ++i) { DatastreamState& state = states_[batchReadings_[i].stateIndex]; if (!transport_.datastreamIdForKey(state.key.c_str(), message.readings[i].datastreamId) || !linkValueFromString(String(batchReadings_[i].value), state.type, message.readings[i].value)) return false; message.readings[i].revision = batchReadings_[i].revision; }
  const bool published = transport_.publishState(message); if (published) batchLastPublishedMs_ = clock_.millisNow(); return published;
}

void FlovaDevice::flushDueStates() { const uint32_t now = clock_.millisNow(); uint8_t pending = 0; for (uint8_t i = 0; i < stateCount_; ++i) { DatastreamState& state = states_[i]; if (!state.publishPending || (state.lastPublishedMs && state.minimumPublishIntervalMs && now - state.lastPublishedMs < state.minimumPublishIntervalMs)) continue; if (!batchMaximumReadings_ || state.mode == FlovaDatastreamMode::Event) publishState(state); else ++pending; } if (!pending || batchReadingCount_) return; if (!batchQueuedAtMs_) batchQueuedAtMs_ = now ? now : 1; if (pending >= batchMaximumReadings_ || now - batchQueuedAtMs_ >= batchFlushIntervalMs_) if (publishStateBatch()) batchQueuedAtMs_ = 0; }
uint64_t FlovaDevice::stateMessageId(const DatastreamState& state) const { return hashText(state.key.c_str()) ^ (static_cast<uint64_t>(bootNonce_) << 32) ^ state.revision; }
void FlovaDevice::flushDirtyStates() { const uint32_t now = clock_.millisNow(); if (static_cast<int32_t>(ingestionRetryNotBeforeMs_ - now) > 0 || now - lastCriticalRetryMs_ < 5000) return; lastCriticalRetryMs_ = now; if (batchReadingCount_) { FlovaLinkStateBatch message = {}; message.messageId = batchMessageId_; message.configurationGeneration = configurationGeneration_; message.count = batchReadingCount_; for (uint8_t i = 0; i < batchReadingCount_; ++i) { DatastreamState& state = states_[batchReadings_[i].stateIndex]; if (!transport_.datastreamIdForKey(state.key.c_str(), message.readings[i].datastreamId) || !linkValueFromString(String(batchReadings_[i].value), state.type, message.readings[i].value)) return; message.readings[i].revision = batchReadings_[i].revision; } if (transport_.publishState(message)) batchLastPublishedMs_ = now; return; } for (uint8_t i = 0; i < stateCount_; ++i) if (states_[i].dirty && states_[i].offline == FlovaOfflinePolicy::KeepLatest) states_[i].publishPending = true; }

FlovaWriteResult FlovaDevice::reportValue(const char* key, const String& value, FlovaValueType type, FlovaValueOrigin origin) {
  if (!datastreamAllowed(key)) {
    FLOVA_DATASTREAM_LOG("[flova] datastream report key=%s accepted=0 reason=unknown_datastream\n", key ? key : "");
    return FlovaWriteResult::reject("unknown_datastream");
  }
  DatastreamState* state = stateFor(key, type, true);
  if (!state || state->type != type || !valueMatchesType(value, type)) {
    FLOVA_DATASTREAM_LOG("[flova] datastream report key=%s type=%s accepted=0 reason=invalid_value\n",
                         key, valueTypeName(type));
    return FlovaWriteResult::reject("invalid_value");
  }
  if (!transport_.connected() && state->offline == FlovaOfflinePolicy::Reject) {
    FLOVA_DATASTREAM_LOG("[flova] datastream report key=%s type=%s accepted=0 reason=offline_delivery_required\n",
                         key, valueTypeName(type));
    return FlovaWriteResult::reject("offline_delivery_required");
  }
  updateState(*state, value, origin, state->offline == FlovaOfflinePolicy::KeepLatest);
  state->publishPending = true;
  bool published = false;
  if (transport_.connected() &&
      (state->mode == FlovaDatastreamMode::Event ||
       (!batchMaximumReadings_ &&
        (!state->lastPublishedMs || !state->minimumPublishIntervalMs ||
         clock_.millisNow() - state->lastPublishedMs >= state->minimumPublishIntervalMs)))) {
    published = publishState(*state);
  }
  FLOVA_DATASTREAM_LOG("[flova] datastream report key=%s type=%s value=%s accepted=1 published=%u revision=%lu\n",
                       key, valueTypeName(type), valueForDatastreamLog(value, type), published ? 1 : 0,
                       static_cast<unsigned long>(state->revision));
  return FlovaWriteResult::accept();
}

FlovaWriteResult FlovaDevice::applyWrite(const char* key, const String& value, FlovaValueType type, FlovaValueOrigin origin, const FlovaLinkId& commandId, const FlovaLinkId& correlationId, uint32_t desiredVersion, bool acknowledgeCloud) {
  if (!datastreamAllowed(key)) return FlovaWriteResult::reject("unknown_datastream");
  DatastreamState* state = stateFor(key, type, true);
  if (!state || state->type != type || !valueMatchesType(value, type)) return FlovaWriteResult::reject("invalid_value");
  if (state->mode == FlovaDatastreamMode::Sample || state->mode == FlovaDatastreamMode::Event) return FlovaWriteResult::reject("not_writable");
  if (state->safetyPolicy != 0) {
    if ((state->safetyPolicy == 1 || state->safetyPolicy == 3) && state->hasSafetyMinimum && value.toDouble() < state->safetyMinimum.toDouble()) return FlovaWriteResult::reject("safety_minimum");
    if ((state->safetyPolicy == 2 || state->safetyPolicy == 3) && state->hasSafetyMaximum && value.toDouble() > state->safetyMaximum.toDouble()) return FlovaWriteResult::reject("safety_maximum");
  }
  if (!transport_.connected() && state->offline == FlovaOfflinePolicy::Reject) return FlovaWriteResult::reject("offline_delivery_required");
  const bool stale = desiredVersion && desiredVersion <= state->lastDesiredVersion, unchanged = state->hasValue && state->value == value;
  const FlovaWriteResult result = stale || unchanged ? FlovaWriteResult::noChange() : invokeWriteHandler(*state, value, commandId, correlationId);
  if (result.accepted() && !stale && !unchanged) { updateState(*state, value, origin, !transport_.connected() && state->offline == FlovaOfflinePolicy::KeepLatest); if (transport_.connected() && !acknowledgeCloud) publishState(*state); }
  if (result.accepted() && desiredVersion > state->lastDesiredVersion) state->lastDesiredVersion = desiredVersion;
  if (acknowledgeCloud) {
    FlovaLinkCommandResult reply = {}; reply.messageId = linkIdHash(commandId); reply.commandId = commandId; reply.correlationId = correlationId; reply.desiredVersion = desiredVersion; reply.status = linkStatus(result); reply.value = {};
    transport_.datastreamIdForKey(key, reply.datastreamId); if (result.accepted()) linkValueFromString(state->value, state->type, reply.value); else copyText(reply.errorCode, result.reasonCode);
    transport_.publishCommandResult(reply);
  }
  return result;
}

void FlovaDevice::setStatusLed(uint8_t pin, bool activeLow) { statusLedPin_ = pin; statusLedActiveLow_ = activeLow; pinMode(pin, OUTPUT); digitalWrite(pin, activeLow ? HIGH : LOW); }
void FlovaDevice::updateStatusIndicator() { statusIndicatorState_ = transport_.connected() ? FlovaStatusIndicatorState::Online : FlovaStatusIndicatorState::Offline; if (statusLedPin_ == 255) return; const uint32_t now = clock_.millisNow(); if (resetGesture_.showingProgress()) { const bool on = (now / 100) % 2 == 0; digitalWrite(statusLedPin_, on == statusLedActiveLow_ ? LOW : HIGH); return; } if (static_cast<int32_t>(resetTapFlashUntilMs_ - now) > 0) { digitalWrite(statusLedPin_, statusLedActiveLow_ ? LOW : HIGH); return; } const bool on = statusIndicatorState_ == FlovaStatusIndicatorState::Online || (now / 500) % 2 == 0; digitalWrite(statusLedPin_, on == statusLedActiveLow_ ? LOW : HIGH); }
void FlovaDevice::setFactoryResetButton(uint8_t pin, bool activeLow, uint32_t holdMs, uint8_t mode) { resetButtonPin_ = pin; resetButtonActiveLow_ = activeLow; pinMode(pin, mode == 255 ? (activeLow ? INPUT_PULLUP : INPUT) : mode); resetGesture_.configure(holdMs); }
void FlovaDevice::processFactoryResetGesture() { if (resetButtonPin_ == 255) return; const uint32_t now = clock_.millisNow(); const bool pressed = digitalRead(resetButtonPin_) == (resetButtonActiveLow_ ? LOW : HIGH); switch (resetGesture_.update(pressed, now)) { case FlovaFactoryResetGesture::TapAccepted: resetTapFlashUntilMs_ = now + 150UL; break; case FlovaFactoryResetGesture::Confirmed: factoryReset(); break; default: break; } }
void FlovaDevice::factoryReset() {
  storage_.clear();
  logger_.info("Factory reset requested.");
#if defined(ESP32) || defined(ESP8266)
  delay(250); ESP.restart();
#endif
}

void FlovaDevice::handleMessage(const FlovaLinkInboundMessage& message) {
  switch (message.type) {
    case FlovaLinkMessageType::Acknowledgement: handleIngestionAck(message.body.acknowledgement); return;
    case FlovaLinkMessageType::Rejection:
      handleIngestionAck(message.body.acknowledgement);
      FLOVA_DATASTREAM_LOG("[flova] Link message rejected id=%llu reason=%s\n",
                           static_cast<unsigned long long>(message.body.acknowledgement.acknowledgedMessageId),
                           message.body.acknowledgement.reasonCode[0] ? message.body.acknowledgement.reasonCode : "unknown");
      return;
    case FlovaLinkMessageType::FlowControl:
      handleIngestionRetry(message.body.acknowledgement);
      FLOVA_DATASTREAM_LOG("[flova] Link flow control id=%llu retry_ms=%lu reason=%s\n",
                           static_cast<unsigned long long>(message.body.acknowledgement.acknowledgedMessageId),
                           static_cast<unsigned long>(message.body.acknowledgement.retryAfterMs),
                           message.body.acknowledgement.reasonCode[0] ? message.body.acknowledgement.reasonCode : "unknown");
      return;
    case FlovaLinkMessageType::TimeResponse: handleTimeSync(message.body.timeResponse); return;
    case FlovaLinkMessageType::ConfigurationBegin:
    case FlovaLinkMessageType::ConfigurationRecord:
    case FlovaLinkMessageType::ConfigurationEnd: handleConfiguration(message.body.configuration); return;
    case FlovaLinkMessageType::BootstrapCommitted: onBootstrapCommitted(message.body.bootstrapCommitted); return;
    case FlovaLinkMessageType::ScheduleRecord: handleScheduleRecord(message.body.schedule); return;
    case FlovaLinkMessageType::OtaOffer: handleOtaOffer(message.body.otaOffer); return;
    case FlovaLinkMessageType::Command: break;
    default: return;
  }
  const FlovaLinkCommand& command = message.body.command;
  if (command.expiresAtUtcMs) {
    if (!clock_.utcValid()) {
      FlovaLinkCommandResult reply = {}; reply.messageId = linkIdHash(command.commandId); reply.commandId = command.commandId; reply.correlationId = command.correlationId; reply.datastreamId = command.datastreamId; reply.status = FlovaLinkResultStatus::Error; copyText(reply.errorCode, "utc_time_required"); transport_.publishCommandResult(reply); return;
    }
    if (clock_.utcMillis() >= command.expiresAtUtcMs) {
      FlovaLinkCommandResult reply = {}; reply.messageId = linkIdHash(command.commandId); reply.commandId = command.commandId; reply.correlationId = command.correlationId; reply.datastreamId = command.datastreamId; reply.status = FlovaLinkResultStatus::Error; copyText(reply.errorCode, "command_expired"); transport_.publishCommandResult(reply); return;
    }
  }
  if (command.datastreamId == FLOVA_FACTORY_RESET_COMPACT_ID &&
      command.value.kind == FlovaLinkValueKind::Bool && command.value.data.boolean) {
    FlovaLinkCommandResult reply = {};
    reply.messageId = linkIdHash(command.commandId);
    reply.commandId = command.commandId;
    reply.correlationId = command.correlationId;
    reply.datastreamId = command.datastreamId;
    reply.status = FlovaLinkResultStatus::Ok;
    reply.value.kind = FlovaLinkValueKind::Bool;
    reply.value.data.boolean = true;
    transport_.publishCommandResult(reply);
    factoryReset();
    return;
  }
  char key[FLOVA_LINK_TEXT_BYTES] = {};
  if (!transport_.datastreamKeyForId(command.configurationGeneration, command.datastreamId, key, sizeof(key))) return;
  String value;
  if (!stringFromLinkValue(command.value, value)) return;
  if (command.isUserCommand && commandSeen(command.commandId)) { acknowledgeDuplicateCommand(command.commandId, command.correlationId, String(key), value); return; }
  if (!datastreamAllowed(key)) { FlovaLinkCommandResult reply = {}; reply.messageId = linkIdHash(command.commandId); reply.commandId = command.commandId; reply.correlationId = command.correlationId; reply.datastreamId = command.datastreamId; reply.status = FlovaLinkResultStatus::Error; copyText(reply.errorCode, "unknown_datastream"); transport_.publishCommandResult(reply); return; }
  DatastreamState* state = const_cast<DatastreamState*>(stateFor(key));
  applyWrite(key, value, state ? state->type : FlovaValueType::String, command.isUserCommand ? FlovaValueOrigin::UserCommand : FlovaValueOrigin::CloudAutomation, command.commandId, command.correlationId, command.desiredVersion, true);
  if (command.isUserCommand) rememberCommand(command.commandId);
}

void FlovaDevice::handleIngestionAck(const FlovaLinkAcknowledgement& acknowledgement) { const uint64_t messageId = acknowledgement.acknowledgedMessageId; if (messageId == batchMessageId_) { for (uint8_t i = 0; i < stateCount_; ++i) if (states_[i].batchInFlight) { if (states_[i].revision == states_[i].batchedRevision) states_[i].dirty = false; states_[i].batchInFlight = false; } batchMessageId_ = 0; batchReadingCount_ = 0; return; } for (uint8_t i = 0; i < stateCount_; ++i) if (messageId == stateMessageId(states_[i])) { states_[i].dirty = false; return; } }
void FlovaDevice::handleIngestionRetry(const FlovaLinkAcknowledgement& acknowledgement) { const uint32_t retryAfter = min<uint32_t>(acknowledgement.retryAfterMs ? acknowledgement.retryAfterMs : 5000, 300000); ingestionRetryNotBeforeMs_ = clock_.millisNow() + retryAfter; }
bool FlovaDevice::commandSeen(const FlovaLinkId& commandId) const { if (!linkIdPresent(commandId)) return false; for (uint8_t i = 0; i < config_.limits.commandDedup; ++i) if (linkIdEqual(recentCommandIds_[i], commandId)) return true; return false; }
void FlovaDevice::rememberCommand(const FlovaLinkId& commandId) { if (!linkIdPresent(commandId) || commandSeen(commandId)) return; recentCommandIds_[recentCommandCursor_] = commandId; recentCommandCursor_ = (recentCommandCursor_ + 1) % config_.limits.commandDedup; }
void FlovaDevice::acknowledgeDuplicateCommand(const FlovaLinkId& commandId, const FlovaLinkId& correlationId, const String& key, const String& value) { const DatastreamState* state = stateFor(key.c_str()); const bool accepted = state && state->hasValue && state->value == value; FlovaLinkCommandResult reply = {}; reply.messageId = linkIdHash(commandId); reply.commandId = commandId; reply.correlationId = correlationId; reply.status = accepted ? FlovaLinkResultStatus::Duplicate : FlovaLinkResultStatus::Error; reply.duplicate = true; transport_.datastreamIdForKey(key.c_str(), reply.datastreamId); if (accepted) linkValueFromString(state->value, state->type, reply.value); else copyText(reply.errorCode, "duplicate_command"); transport_.publishCommandResult(reply); }

void FlovaDevice::handleConfiguration(const FlovaLinkConfigurationRecord& record) {
  if (record.phase == FlovaLinkConfigurationPhase::Record &&
      (record.recordLength > FLOVA_LINK_RECORD_BYTES || !record.hasTypedUnit)) return;
  if (configurationApplyPending_) return;
  pendingConfiguration_ = record;
  configurationApplyPending_ = true;
}

void FlovaDevice::applyPendingConfiguration() {
  if (!configurationApplyPending_) return;
  configurationApplyPending_ = false;
  const FlovaLinkConfigurationRecord& record = pendingConfiguration_;
  bool persisted = applyConfigurationRecord(record);
  // Runtime transfers are transactional: records are persisted and verified,
  // but never mutate live mappings/hardware one at a time. CONFIG_END causes a
  // clean reboot, then restoreActiveConfiguration() applies the whole verified
  // generation. An interrupted transfer therefore cannot create a mixed runtime.
  const bool applied = persisted;
  FlovaLinkConfigurationReport report = {};
  report.messageId = record.messageId;
  report.generation = record.generation;
  report.sequence = record.phase == FlovaLinkConfigurationPhase::End
                        ? record.recordCount
                        : record.sequence;
  memcpy(report.checksum, record.checksum, sizeof(report.checksum));
  report.status = persisted && applied ? FlovaLinkResultStatus::Ok : FlovaLinkResultStatus::Error;
  if (!persisted) copyText(report.errorCode, "configuration_record_rejected");
  else if (!applied) copyText(report.errorCode, "configuration_runtime_rejected");
  transport_.publishConfigurationReport(report);
  if (persisted && record.phase == FlovaLinkConfigurationPhase::End &&
      !configurationRuntimeDeferred_) onConfigurationCommitted(record.generation);
}

bool FlovaDevice::applyConfigurationUnit(const flova::config::Unit& unit) {
  if (unit.kind == flova::config::UnitKind::System) {
    if (unit.data.system.hasHeartbeatMs) {
      if (!unit.data.system.heartbeatMs || unit.data.system.heartbeatMs > 86400000UL) return false;
      config_.heartbeatIntervalMs = unit.data.system.heartbeatMs;
    }
    if (unit.data.system.hasBatchFlushMs) {
      if (!unit.data.system.batchFlushMs || unit.data.system.batchFlushMs > 60000UL) return false;
      enableStateBatching(batchMaximumReadings_ ? batchMaximumReadings_ : 1, unit.data.system.batchFlushMs);
    }
    if (unit.data.system.hasStatusLedPin) {
      setStatusLed(unit.data.system.statusLedPin,
                   unit.data.system.hasStatusLedActiveLow && unit.data.system.statusLedActiveLow);
    }
    return true;
  }
  if (unit.kind == flova::config::UnitKind::Datastream) {
    if (unit.data.datastream.valueType == 1) return false;
    FlovaValueType valueType = unit.data.datastream.valueType == 0 ? FlovaValueType::Bool :
                                unit.data.datastream.valueType == 2 ? FlovaValueType::Float :
                                unit.data.datastream.valueType == 3 ? FlovaValueType::Number :
                                unit.data.datastream.valueType == 4 ? FlovaValueType::String : FlovaValueType::String;
    if (unit.data.datastream.valueType > 4) return false;
    DatastreamState* state = stateFor(unit.data.datastream.key, valueType, true);
    if (!state) return false;
    state->type = valueType;
    FLOVA_DATASTREAM_LOG("[flova] datastream configured key=%s id=%u type=%s mapping=%u\n",
                         unit.data.datastream.key, unit.data.datastream.compactId,
                         valueTypeName(valueType), unit.data.datastream.hasMapping ? 1 : 0);
    if (unit.data.datastream.hasMapping) {
      double minimum = 0, maximum = 100;
      bool hasRange = false;
      if (unit.data.datastream.hasMinimum && unit.data.datastream.minimum.kind != flova::config::ValueKind::Text &&
          unit.data.datastream.hasMaximum && unit.data.datastream.maximum.kind != flova::config::ValueKind::Text) {
        minimum = unit.data.datastream.minimum.kind == flova::config::ValueKind::Float32 ? unit.data.datastream.minimum.data.float32 : unit.data.datastream.minimum.data.float64;
        maximum = unit.data.datastream.maximum.kind == flova::config::ValueKind::Float32 ? unit.data.datastream.maximum.data.float32 : unit.data.datastream.maximum.data.float64;
        hasRange = minimum < maximum;
      }
      if (!applyHardwareMapping(unit.data.datastream.key, unit.data.datastream.mapping, minimum, maximum, hasRange)) return false;
    }
    return true;
  }
  if (unit.kind != flova::config::UnitKind::Safety) return true;
  char key[FLOVA_LINK_TEXT_BYTES] = {};
  if (!transport_.datastreamKeyForId(configurationApplyingGeneration_, unit.data.safety.compactId, key, sizeof(key))) return false;
  DatastreamState* state = stateFor(key, FlovaValueType::String, false);
  if (!state) return false;
  state->safetyPolicy = static_cast<uint8_t>(unit.data.safety.policy);
  state->hasSafetyMinimum = unit.data.safety.hasMinimum && unit.data.safety.minimum.kind != flova::config::ValueKind::Text;
  state->hasSafetyMaximum = unit.data.safety.hasMaximum && unit.data.safety.maximum.kind != flova::config::ValueKind::Text;
  if (state->hasSafetyMinimum) state->safetyMinimum = unit.data.safety.minimum.kind == flova::config::ValueKind::Float32 ? String(unit.data.safety.minimum.data.float32) : String(unit.data.safety.minimum.data.float64);
  if (state->hasSafetyMaximum) state->safetyMaximum = unit.data.safety.maximum.kind == flova::config::ValueKind::Float32 ? String(unit.data.safety.maximum.data.float32) : String(unit.data.safety.maximum.data.float64);
  return true;
}

bool FlovaDevice::applyHardwareMapping(const char* key, const flova::config::HardwareMapping& mapping,
                                       double minimum, double maximum, bool hasRange) {
  if (!key || !key[0] || mapping.pin == 255) {
    FLOVA_DATASTREAM_LOG("[flova] datastream mapping rejected key=%s pin=%u reason=invalid_mapping\n", key ? key : "", mapping.pin);
    return false;
  }
  const uint8_t mode = mapping.hasPull && mapping.pull == 1 ? INPUT_PULLUP : INPUT;
  FLOVA_DATASTREAM_LOG("[flova] datastream mapping key=%s kind=%s pin=%u active_high=%u pull=%s\n",
                       key, mappingKindName(mapping.kind), mapping.pin,
                       !mapping.hasActiveHigh || mapping.activeHigh ? 1 : 0,
                       mode == INPUT_PULLUP ? "up" : "none");
  if (mapping.kind == flova::config::MappingKind::DigitalOutput) {
    for (uint8_t i = 0; i < outputCount_; ++i) if (outputs_[i].key == key) {
      outputs_[i].pin = mapping.pin; outputs_[i].activeHigh = !mapping.hasActiveHigh || mapping.activeHigh;
      if (mapping.hasMinimumOutputMs) outputs_[i].minOutputIntervalMs = mapping.minimumOutputMs;
      pinMode(outputs_[i].pin, OUTPUT); return true;
    }
    addDigitalOutput(key, static_cast<uint8_t>(mapping.pin), !mapping.hasActiveHigh || mapping.activeHigh,
                     mapping.hasMinimumOutputMs ? mapping.minimumOutputMs : 300);
    return true;
  }
  if (mapping.kind == flova::config::MappingKind::PwmOutput) {
    for (uint8_t i = 0; i < pwmOutputCount_; ++i) if (pwmOutputs_[i].key == key) {
      pwmOutputs_[i].pin = mapping.pin; if (hasRange) { pwmOutputs_[i].minimum = minimum; pwmOutputs_[i].maximum = maximum; }
      pinMode(pwmOutputs_[i].pin, OUTPUT); return true;
    }
    addPwmOutput(key, static_cast<uint8_t>(mapping.pin), hasRange ? minimum : 0,
                 hasRange ? maximum : 100, hasRange ? minimum : 0);
    return true;
  }
  if (mapping.kind == flova::config::MappingKind::DigitalInput) {
    for (uint8_t i = 0; i < inputCount_; ++i) if (inputs_[i].key == key) {
      inputs_[i].pin = mapping.pin; inputs_[i].activeHigh = !mapping.hasActiveHigh || mapping.activeHigh;
      if (mapping.hasDebounceMs) inputs_[i].debounceMs = mapping.debounceMs;
      pinMode(inputs_[i].pin, mode); return true;
    }
    if (inputCount_ + analogInputCount_ >= config_.limits.hardwareInputs) {
      FLOVA_DATASTREAM_LOG("[flova] datastream mapping rejected key=%s pin=%u reason=hardware_input_capacity\n", key, mapping.pin);
      return false;
    }
    addDigitalInput(key, static_cast<uint8_t>(mapping.pin), !mapping.hasActiveHigh || mapping.activeHigh,
                    mapping.hasDebounceMs ? mapping.debounceMs : 50, mode);
    return true;
  }
  if (mapping.kind == flova::config::MappingKind::AnalogInput) {
    for (uint8_t i = 0; i < analogInputCount_; ++i) if (analogInputs_[i].key == key) {
      analogInputs_[i].pin = mapping.pin;
      if (mapping.hasSampleMs) analogInputs_[i].sampleIntervalMs = max<uint32_t>(100, mapping.sampleMs);
      pinMode(analogInputs_[i].pin, INPUT); return true;
    }
    addAnalogInput(key, static_cast<uint8_t>(mapping.pin), mapping.hasSampleMs ? mapping.sampleMs : 1000);
    return true;
  }
  return false;
}
void FlovaDevice::handleScheduleRecord(const FlovaLinkScheduleRecord& record) { if (record.recordLength > FLOVA_LINK_RECORD_BYTES) return; const bool ok = applyScheduleRecord(record); if (ok) { scheduleRevision_ = record.revision; scheduleExpiryReported_ = false; } reportScheduleStatus(ok ? "installed" : "schedule_record_rejected"); }
void FlovaDevice::handleOtaOffer(const FlovaLinkOtaOffer& offer) { if (!config_.otaCapable || !otaInstaller_) { pendingOtaOffer_ = offer; pendingOta_ = true; reportOta(FlovaLinkResultStatus::Error, "ota_not_supported"); pendingOta_ = false; return; } pendingOtaOffer_ = offer; pendingOta_ = true; reportOta(FlovaLinkResultStatus::Ok, nullptr); }
void FlovaDevice::processPendingOta() {
  if (!pendingOta_ || !otaInstaller_) return;
  pendingOta_ = false;

  // Keep the Link offer in the device workspace. Copying this 500-byte frame
  // consumed a quarter of the ESP8266 continuation stack.
  const FlovaLinkOtaOffer& pending = pendingOtaOffer_;
  if (!linkIdPresent(pending.installId) ||
      !memchr(pending.url, 0, sizeof(pending.url))) {
    reportOta(FlovaLinkResultStatus::Error, "invalid_offer");
    return;
  }

  char installId[33] = {}, releaseId[33] = {};
  linkIdHex(pending.installId, installId);
  linkIdHex(pending.releaseId, releaseId);
  FlovaOtaOffer offer;
  offer.installId = installId;
  offer.releaseId = releaseId;
  offer.version = pending.version;
  offer.firmwareTarget = pending.firmwareTarget;
  offer.artifactUrl = pending.url;
  offer.sha256 = pending.sha256;
  offer.sizeBytes = pending.sizeBytes;

  reportOta(FlovaLinkResultStatus::Ok, nullptr);
  transport_.disconnect();
  const FlovaOtaResult result = otaInstaller_->install(offer);
  if (result != FlovaOtaResult::Installed) {
    const char* code = result == FlovaOtaResult::HashMismatch
                           ? "checksum_mismatch"
                       : result == FlovaOtaResult::DownloadFailed
                           ? "download_failed"
                       : result == FlovaOtaResult::ResourceUnavailable
                           ? "resource_unavailable"
                           : "flash_failed";
    transport_.connect(config_.deviceId, config_.linkSecret);
    reportOta(FlovaLinkResultStatus::Error, code);
    return;
  }

  storage_.setString("ota_release", offer.releaseId.c_str());
  storage_.setString("ota_install", offer.installId.c_str());
  storage_.setString("ota_version", offer.version.c_str());
  transport_.connect(config_.deviceId, config_.linkSecret);
  reportOta(FlovaLinkResultStatus::Ok, nullptr);
  delay(100);
#if defined(ESP32) || defined(ESP8266)
  ESP.restart();
#endif
}
void FlovaDevice::reportOta(FlovaLinkResultStatus status, const char* errorCode) { if (!transport_.connected() && status != FlovaLinkResultStatus::Error) return; FlovaLinkOtaReport report = {}; report.messageId = nextLinkId(bootNonce_, clock_.millisNow()); report.status = status; report.installId = pendingOtaOffer_.installId; if (errorCode) copyText(report.errorCode, errorCode); transport_.publishOtaReport(report); }

bool FlovaDevice::handleMappedWrite(const FlovaLinkId& commandId, const FlovaLinkId& correlationId, const String& key, const String& value, uint32_t desiredVersion) { for (uint8_t i = 0; i < outputCount_; ++i) if (outputs_[i].key == key) { const bool on = value == "true" || value == "1"; if (desiredVersion && desiredVersion <= outputs_[i].lastAppliedDesiredVersion) { ackDigitalOutput(outputs_[i], commandId, correlationId); return true; } const uint32_t now = clock_.millisNow(); if (!outputs_[i].lastAppliedMs || now - outputs_[i].lastAppliedMs >= outputs_[i].minOutputIntervalMs) { applyDigitalOutput(outputs_[i], on); outputs_[i].lastAppliedDesiredVersion = desiredVersion; ackDigitalOutput(outputs_[i], commandId, correlationId); } else { outputs_[i].pending = true; outputs_[i].pendingValue = on; outputs_[i].pendingCommandId = commandId; outputs_[i].pendingCorrelationId = correlationId; outputs_[i].lastAppliedDesiredVersion = desiredVersion; } return true; } return false; }
void FlovaDevice::applyDigitalOutput(DigitalOutput& output, bool value) { output.value = value; output.pending = false; output.lastAppliedMs = clock_.millisNow(); digitalWrite(output.pin, value == output.activeHigh ? HIGH : LOW); }
void FlovaDevice::ackDigitalOutput(const DigitalOutput& output, const FlovaLinkId& commandId, const FlovaLinkId& correlationId) { FlovaLinkCommandResult reply = {}; reply.messageId = linkIdHash(commandId); reply.commandId = commandId; reply.correlationId = correlationId; reply.desiredVersion = output.lastAppliedDesiredVersion; reply.status = FlovaLinkResultStatus::Ok; transport_.datastreamIdForKey(output.key.c_str(), reply.datastreamId); reply.value.kind = FlovaLinkValueKind::Bool; reply.value.data.boolean = output.value; transport_.publishCommandResult(reply); }
void FlovaDevice::flushDigitalOutputs() { const uint32_t now = clock_.millisNow(); for (uint8_t i = 0; i < outputCount_; ++i) if (outputs_[i].pending && now - outputs_[i].lastAppliedMs >= outputs_[i].minOutputIntervalMs) { applyDigitalOutput(outputs_[i], outputs_[i].pendingValue); ackDigitalOutput(outputs_[i], outputs_[i].pendingCommandId, outputs_[i].pendingCorrelationId); outputs_[i].pendingCommandId = {}; outputs_[i].pendingCorrelationId = {}; } }
void FlovaDevice::pollDigitalInputs() {
  const uint32_t now = clock_.millisNow();
  for (uint8_t i = 0; i < inputCount_; ++i) {
    const bool raw = digitalRead(inputs_[i].pin) == (inputs_[i].activeHigh ? HIGH : LOW);
    if (raw != inputs_[i].lastRaw) {
      inputs_[i].lastRaw = raw;
      inputs_[i].changedAt = now;
    }
    if (raw != inputs_[i].lastSent && now - inputs_[i].changedAt >= inputs_[i].debounceMs) {
      inputs_[i].lastSent = raw;
      FLOVA_DATASTREAM_LOG("[flova] datastream input edge key=%s pin=%u raw=%u value=%s\n",
                           inputs_[i].key.c_str(), inputs_[i].pin, raw ? 1 : 0, raw ? "true" : "false");
      reportValue(inputs_[i].key.c_str(), raw ? "true" : "false", FlovaValueType::Bool,
                  FlovaValueOrigin::PhysicalInput);
    }
  }
}
void FlovaDevice::pollAnalogInputs() { const uint32_t now = clock_.millisNow(); for (uint8_t i = 0; i < analogInputCount_; ++i) { AnalogInput& input = analogInputs_[i]; if (input.sampled && now - input.lastSampleMs < input.sampleIntervalMs) continue; input.sampled = true; input.lastSampleMs = now; reportValue(input.key.c_str(), String(analogRead(input.pin)), FlovaValueType::Number, FlovaValueOrigin::PhysicalInput); } }
FlovaWriteResult FlovaDevice::applyPwmOutput(PwmOutput& output, const String& value) { const double number = value.toDouble(); if (number < output.minimum || number > output.maximum) return FlovaWriteResult::reject("out_of_range");
#if defined(ESP8266)
  const uint16_t nativeMaximum = 1023;
#else
  const uint16_t nativeMaximum = 255;
#endif
  analogWrite(output.pin, static_cast<uint16_t>(round((number - output.minimum) * nativeMaximum / (output.maximum - output.minimum)))); return FlovaWriteResult::accept(); }

bool FlovaDevice::publishHeartbeat() { lastHeartbeatMs_ = clock_.millisNow(); FlovaLinkHeartbeat message = {}; message.messageId = nextLinkId(bootNonce_, lastHeartbeatMs_); message.configurationGeneration = configurationGeneration_; message.uptimeMs = lastHeartbeatMs_; message.protocolVersion = config_.protocolVersion; message.datastreamSlots = config_.capabilities.datastreamSlots; message.scheduleSlots = config_.capabilities.scheduleSlots; message.otaCapable = config_.otaCapable; copyText(message.firmwareVersion, config_.firmwareVersion); copyText(message.firmwareTarget, config_.firmwareTarget); copyText(message.runningReleaseId, config_.runningReleaseId); copyText(message.appliedTemplateVersionId, config_.appliedTemplateVersionId); const bool ok = transport_.publishHeartbeat(message); if (ok && bootControl_ && bootControl_->state() == FlovaBootState::Candidate) candidateHeartbeatPublished_ = true; logger_.info(ok ? "Heartbeat published." : "Heartbeat failed."); return ok; }
bool FlovaDevice::publishConfigurationState() { if (!transport_.connected()) return false; FlovaLinkConfigurationState message = {}; message.messageId = nextLinkId(bootNonce_, clock_.millisNow() ^ configurationGeneration_); message.generation = configurationGeneration_; memcpy(message.checksum, configurationChecksum_, sizeof(message.checksum)); message.status = configurationValid_ ? FlovaLinkResultStatus::Ok : FlovaLinkResultStatus::Error; return transport_.publishConfigurationState(message); }
bool FlovaDevice::reconnect() { const uint32_t now = clock_.millisNow(); if (transport_.connected()) return true; if (now - lastReconnectMs_ < reconnectDelayMs_) return false; lastReconnectMs_ = now; logger_.info("Device link connecting."); const bool ok = transport_.connect(config_.deviceId, config_.linkSecret); if (!ok) { reconnectBackoffCeilingMs_ = min<uint32_t>(max<uint32_t>(reconnectBackoffCeilingMs_ * 2, 1000), 60000); reconnectDelayMs_ = random(1000, reconnectBackoffCeilingMs_ + 1); logger_.info("Device link connect failed."); return false; } logger_.info("Device link connected."); connectedSinceMs_ = now ? now : 1; publishConfigurationState(); publishHeartbeat(); requestTimeSync(); return true; }

void FlovaDevice::restoreScheduleManifest() {}
void FlovaDevice::runSchedules() { if (!clock_.utcValid() || !scheduleRevision_) return; const uint64_t now = clock_.utcMillis(); if (scheduleValidUntil_ && now >= scheduleValidUntil_) { if (!scheduleExpiryReported_) { reportScheduleStatus("schedule_horizon_expired"); scheduleExpiryReported_ = true; } requestScheduleRenewal(); return; } if (scheduleRenewBefore_ && now >= scheduleRenewBefore_) requestScheduleRenewal(); }
void FlovaDevice::requestScheduleRenewal() { const uint64_t now = clock_.utcMillis(); if (!transport_.connected() || (lastScheduleRenewRequest_ && now - lastScheduleRenewRequest_ < 21600000ULL)) return; FlovaLinkScheduleStatus renew = {}; renew.messageId = nextLinkId(bootNonce_, static_cast<uint32_t>(now)); renew.revision = scheduleRevision_; renew.validUntilUtcMs = scheduleValidUntil_; renew.status = FlovaLinkResultStatus::Ok; if (transport_.publishScheduleRenew(renew)) lastScheduleRenewRequest_ = now; }
void FlovaDevice::reportScheduleStatus(const char* status) { if (!transport_.connected()) return; FlovaLinkScheduleStatus report = {}; report.messageId = nextLinkId(bootNonce_, static_cast<uint32_t>(clock_.millisNow())); report.revision = scheduleRevision_; report.validUntilUtcMs = scheduleValidUntil_; report.status = strcmp(status, "installed") == 0 ? FlovaLinkResultStatus::Ok : FlovaLinkResultStatus::Error; copyText(report.errorCode, status); transport_.publishScheduleStatus(report); }
void FlovaDevice::requestTimeSync() { const uint32_t now = clock_.millisNow(); if (pendingTimeRequestId_ && now - timeRequestStartedMs_ < 30000) return; pendingTimeRequestId_ = nextLinkId(bootNonce_, now); FlovaLinkTimeRequest request = {}; request.messageId = pendingTimeRequestId_; request.monotonicMs = now; if (transport_.publishTimeRequest(request)) { timeRequestStartedMs_ = now; lastTimeRequestMs_ = now; } else pendingTimeRequestId_ = 0; }
void FlovaDevice::handleTimeSync(const FlovaLinkTimeResponse& response) { if (!pendingTimeRequestId_ || response.requestId != pendingTimeRequestId_ || !response.serverUtcMs) return; const uint32_t now = clock_.millisNow(), uncertainty = (now - timeRequestStartedMs_) / 2; clock_.setUtc(response.serverUtcMs + uncertainty, uncertainty); timeRequestStartedMs_ = 0; pendingTimeRequestId_ = 0; }
bool FlovaDevice::datastreamAllowed(const char* key) const { uint16_t id = 0; return key && *key && transport_.datastreamIdForKey(key, id); }
