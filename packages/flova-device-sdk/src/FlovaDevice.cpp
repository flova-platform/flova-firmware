#include "FlovaDevice.h"
#include "FlovaScheduleRuntime.h"
#include <ArduinoJson.h>

static FlovaDevice* activeDevice = nullptr;

static void dispatchMessage(const String& topic, const String& payload) {
  if (activeDevice) activeDevice->handleMessage(topic, payload);
}

void FlovaDevice::configure(const FlovaConfig& config) { config_ = config; initializeResourceContract(); }

void FlovaDevice::initializeResourceContract() {
  config_.capabilities.datastreamSlots = sizeof(states_) / sizeof(states_[0]);
  config_.capabilities.hardwareInputSlots = sizeof(inputs_) / sizeof(inputs_[0]);
  config_.capabilities.hardwareOutputSlots = sizeof(outputs_) / sizeof(outputs_[0]);
  config_.capabilities.commandDedupSlots = sizeof(recentCommandIds_) / sizeof(recentCommandIds_[0]);
  config_.capabilities.scheduleSlots =
      FLOVA_SCHEDULE_RUNTIME_ENABLED ? FLOVA_SCHEDULE_CAPACITY : 0;
  if (!FLOVA_SCHEDULE_RUNTIME_ENABLED) config_.capabilities.scheduleManifestBytes = 0;
  else if (!config_.capabilities.scheduleManifestBytes)
    config_.capabilities.scheduleManifestBytes = 3800;
  if (!FLOVA_HISTORY_RUNTIME_ENABLED) config_.capabilities.historyBytes = 0;
  if (!config_.capabilities.messageBytes) config_.capabilities.messageBytes = 4096;
  if (!config_.limits.datastreams || config_.limits.datastreams > config_.capabilities.datastreamSlots) config_.limits.datastreams = config_.capabilities.datastreamSlots;
  if (!config_.limits.hardwareInputs || config_.limits.hardwareInputs > config_.capabilities.hardwareInputSlots) config_.limits.hardwareInputs = config_.capabilities.hardwareInputSlots;
  if (!config_.limits.hardwareOutputs || config_.limits.hardwareOutputs > config_.capabilities.hardwareOutputSlots) config_.limits.hardwareOutputs = config_.capabilities.hardwareOutputSlots;
  if (!config_.limits.commandDedup || config_.limits.commandDedup > config_.capabilities.commandDedupSlots) config_.limits.commandDedup = config_.capabilities.commandDedupSlots;
  if (!config_.limits.messageBytes || config_.limits.messageBytes > config_.capabilities.messageBytes) config_.limits.messageBytes = config_.capabilities.messageBytes;
}

bool FlovaDevice::begin() {
  activeDevice = this;
  transport_.setCallback(dispatchMessage);
  char storedCommands[160] = {0};
  if (storage_.getString("command_ids", storedCommands, sizeof(storedCommands))) {
    String stored(storedCommands);
    for (uint8_t i = 0; i < config_.limits.commandDedup && stored.length(); i++) {
      int separator = stored.indexOf(',');
      recentCommandIds_[i] = separator < 0 ? stored : stored.substring(0, separator);
      stored = separator < 0 ? "" : stored.substring(separator + 1);
      recentCommandCursor_ = (i + 1) % config_.limits.commandDedup;
    }
  }
  restorePersistentStates();
  restoreScheduleManifest();
  return transport_.begin() && reconnect();
}

void FlovaDevice::loop() {
  transport_.loop();
  processPendingOta();
  if (!transport_.connected()) reconnect();
  updateCandidateHealth();
  updateStatusIndicator();
  if (resetButtonPin_ != 255) {
    bool pressed = digitalRead(resetButtonPin_) == (resetButtonActiveLow_ ? LOW : HIGH);
    if (pressed && resetStartedMs_ == 0) resetStartedMs_ = clock_.millisNow();
    if (!pressed) resetStartedMs_ = 0;
    if (pressed && clock_.millisNow() - resetStartedMs_ >= resetHoldMs_) factoryReset();
  }

  uint32_t now = clock_.millisNow();
  if (transport_.connected() && now - lastHeartbeatMs_ >= config_.heartbeatIntervalMs) {
    publishHeartbeat();
  }
  if (transport_.connected() && (!clock_.utcValid() || now - lastTimeRequestMs_ >= 21600000UL)) requestTimeSync();
  if (transport_.connected()) pollDigitalInputs();
  flushDigitalOutputs();
  if (transport_.connected()) flushDirtyStates();
  runSchedules();
}

void FlovaDevice::updateCandidateHealth() {
  if (!bootControl_ || bootControl_->state() != FlovaBootState::Candidate) return;
  uint32_t now = clock_.millisNow();
  if (!candidateStartedMs_) candidateStartedMs_ = now ? now : 1;
  if (transport_.connected() && candidateHeartbeatPublished_) {
    if (!candidateHealthySinceMs_) candidateHealthySinceMs_ = now ? now : 1;
    if (now - candidateHealthySinceMs_ >= 30000UL) {
      if (bootControl_->confirmCandidate()) publishHeartbeat();
      return;
    }
  } else {
    candidateHealthySinceMs_ = 0;
  }
  if (now - candidateStartedMs_ >= 120000UL) {
    reportOta("rollback_requested", "health_timeout");
    bootControl_->rollbackCandidate();
  }
}

void FlovaDevice::addDigitalOutput(const char* key, uint8_t pin, bool activeHigh, uint32_t minOutputIntervalMs) {
  if (outputCount_ >= config_.limits.hardwareOutputs) return;
  outputs_[outputCount_].key = String(key);
  outputs_[outputCount_].pin = pin;
  outputs_[outputCount_].activeHigh = activeHigh;
  outputs_[outputCount_].minOutputIntervalMs = minOutputIntervalMs;
  outputCount_++;
  DatastreamState* state = stateFor(key, FlovaValueType::Bool, true);
  if (state) { state->type = FlovaValueType::Bool; state->value = "false"; state->hasValue = true; state->quality = FlovaValueQuality::Good; }
  pinMode(pin, OUTPUT);
  digitalWrite(pin, activeHigh ? LOW : HIGH);
}

void FlovaDevice::addDigitalInput(const char* key, uint8_t pin, bool activeHigh, uint32_t debounceMs, uint8_t mode) {
  if (inputCount_ >= config_.limits.hardwareInputs) return;
  pinMode(pin, mode);
  bool raw = digitalRead(pin) == (activeHigh ? HIGH : LOW);
  inputs_[inputCount_].key = String(key);
  inputs_[inputCount_].pin = pin;
  inputs_[inputCount_].activeHigh = activeHigh;
  inputs_[inputCount_].debounceMs = debounceMs;
  inputs_[inputCount_].lastRaw = raw;
  inputs_[inputCount_].lastSent = !raw;
  inputs_[inputCount_].changedAt = clock_.millisNow();
  inputCount_++;
}

FlovaDevice::DatastreamState* FlovaDevice::stateFor(const char* key, FlovaValueType type, bool create) {
  for (uint8_t i = 0; i < stateCount_; ++i) if (states_[i].key == key) return &states_[i];
  if (!create || stateCount_ >= config_.limits.datastreams) return nullptr;
  states_[stateCount_].key = key;
  states_[stateCount_].type = type;
  return &states_[stateCount_++];
}

const FlovaDevice::DatastreamState* FlovaDevice::stateFor(const char* key) const {
  for (uint8_t i = 0; i < stateCount_; ++i) if (states_[i].key == key) return &states_[i];
  return nullptr;
}

bool FlovaDevice::valueMatchesType(const String& value, FlovaValueType type) const {
  if (type == FlovaValueType::Bool) return value == "true" || value == "false" || value == "1" || value == "0";
  if (type == FlovaValueType::String) return true;
  if (type == FlovaValueType::Object) {
    DynamicJsonDocument document(min<size_t>(config_.limits.messageBytes, 4096));
    return !deserializeJson(document, value) && document.is<JsonObject>();
  }
  if (!value.length()) return false;
  bool decimal = false;
  for (uint16_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '-' && i == 0) continue;
    if (c == '.' && !decimal) { decimal = true; continue; }
    if (c < '0' || c > '9') return false;
  }
  return true;
}

void FlovaDevice::updateState(DatastreamState& state, const String& value, FlovaValueOrigin origin, bool dirty) {
  state.value = value; state.hasValue = true; state.origin = origin; state.quality = FlovaValueQuality::Good;
  state.updatedAt = clock_.millisNow(); state.revision++; state.dirty = dirty;
  if (state.persistence == FlovaPersistencePolicy::Persistent) persistState(state);
}

void FlovaDevice::persistState(const DatastreamState& state) {
  // Include the key in the record so a bounded/hash-based storage adapter can
  // detect collisions and ignore a snapshot rather than restoring wrong state.
  String record = String((int)state.type) + "|" + state.key + "|" + String(state.revision) + "|" + state.value;
  storage_.setString(("ds:" + state.key).c_str(), record.c_str());
}

void FlovaDevice::restorePersistentStates() {
  for (uint8_t i = 0; i < stateCount_; ++i) {
    if (states_[i].persistence != FlovaPersistencePolicy::Persistent) continue;
    char stored[128] = {0};
    if (!storage_.getString(("ds:" + states_[i].key).c_str(), stored, sizeof(stored))) continue;
    String row(stored); int first = row.indexOf('|'); int second = row.indexOf('|', first + 1); int third = row.indexOf('|', second + 1);
    if (first < 1 || second <= first || third <= second || row.substring(first + 1, second) != states_[i].key) continue;
    FlovaValueType type = (FlovaValueType)row.substring(0, first).toInt(); String value = row.substring(third + 1);
    if (type == states_[i].type && valueMatchesType(value, type)) { states_[i].value = value; states_[i].hasValue = true; states_[i].revision = row.substring(second + 1, third).toInt(); states_[i].origin = FlovaValueOrigin::DeviceRestore; states_[i].quality = FlovaValueQuality::Good; }
  }
}

bool FlovaDevice::readCached(const char* key, String& value, uint32_t* revision) const {
  const DatastreamState* state = stateFor(key); if (!state || !state->hasValue) return false;
  value = state->value; if (revision) *revision = state->revision; return true;
}
bool FlovaDevice::readSnapshotMetadata(const char* key, uint32_t& updatedAt, FlovaValueOrigin& origin,
                                       FlovaValueQuality& quality, bool& dirty, uint32_t& revision) const {
  const DatastreamState* state = stateFor(key); if (!state) return false;
  updatedAt = state->updatedAt; origin = state->origin; quality = state->quality; dirty = state->dirty; revision = state->revision; return state->hasValue;
}
bool FlovaDevice::hasValue(const char* key) const { String ignored; return readCached(key, ignored); }

bool FlovaDevice::registerTypedWrite(const char* key, void* handler, FlovaValueType type) {
  DatastreamState* state = stateFor(key, type, true); if (!state) return false; state->type = type; state->writeHandler = handler; return true;
}
bool FlovaDevice::registerTypedRead(const char* key, void* handler, FlovaValueType type) {
  DatastreamState* state = stateFor(key, type, true); if (!state) return false; state->type = type; state->readHandler = handler; return true;
}
void FlovaDevice::configureDatastream(const char* key, FlovaDatastreamMode mode, FlovaOfflinePolicy offline, FlovaPersistencePolicy persistence, FlovaRestorePolicy restore) {
  DatastreamState* state = stateFor(key, FlovaValueType::String, true); if (!state) return;
  state->mode = mode; state->offline = offline; state->persistence = persistence; state->restore = restore;
}

FlovaWriteResult FlovaDevice::invokeWriteHandler(DatastreamState& state, const String& value,
                                                 const String& commandId,
                                                 const String& correlationId) {
  if (state.writeHandler) {
    if (state.type == FlovaValueType::Bool) return reinterpret_cast<FlovaWriteResult (*)(bool)>(state.writeHandler)(value == "true" || value == "1");
    if (state.type == FlovaValueType::Float) return reinterpret_cast<FlovaWriteResult (*)(float)>(state.writeHandler)(value.toFloat());
    if (state.type == FlovaValueType::Number) return reinterpret_cast<FlovaWriteResult (*)(double)>(state.writeHandler)(value.toDouble());
    if (state.type == FlovaValueType::Object)
      return reinterpret_cast<FlovaWriteResult (*)(FlovaObjectValue)>(state.writeHandler)(
          FlovaObjectValue(value, commandId, correlationId));
    return reinterpret_cast<FlovaWriteResult (*)(String)>(state.writeHandler)(value);
  }
  for (uint8_t i = 0; i < outputCount_; ++i) if (outputs_[i].key == state.key) { applyDigitalOutput(outputs_[i], value == "true" || value == "1"); return FlovaWriteResult::accept(); }
  return FlovaWriteResult::failure("write_handler_missing");
}

bool FlovaDevice::publishState(DatastreamState& state) {
  String payload = "{\"protocol\":{\"name\":\"flova\",\"version\":1},\"schema_version\":1,\"message_id\":\"state-" + state.key + "-" + String(state.revision) + "\",\"key\":\"" + state.key + "\",\"value\":" + jsonScalar(state.value) + ",\"revision\":" + String(state.revision);
  String ts = clock_.isoNow(); if (ts.length()) payload += ",\"ts\":\"" + ts + "\""; payload += "}";
  if (transport_.publish(datastreamTopic(state.key.c_str(), "update").c_str(), payload)) { state.dirty = false; return true; }
  return false;
}

void FlovaDevice::flushDirtyStates() {
  // KeepLatest uses the snapshot itself as the queue: many offline writes cost
  // one slot and reconnect publishes only the final accepted state.
  for (uint8_t i = 0; i < stateCount_; ++i) if (states_[i].dirty && states_[i].offline == FlovaOfflinePolicy::KeepLatest) publishState(states_[i]);
}

FlovaWriteResult FlovaDevice::reportValue(const char* key, const String& value, FlovaValueType type, FlovaValueOrigin origin) {
  if (!datastreamAllowed(key)) return FlovaWriteResult::reject("unknown_datastream");
  DatastreamState* state = stateFor(key, type, true);
  if (!state || state->type != type || !valueMatchesType(value, type)) return FlovaWriteResult::reject("invalid_value");
  if (!transport_.connected() && state->offline == FlovaOfflinePolicy::Reject) return FlovaWriteResult::reject("offline_delivery_required");
  bool dirty = !transport_.connected() && state->offline == FlovaOfflinePolicy::KeepLatest;
  updateState(*state, value, origin, dirty);
  if (transport_.connected()) publishState(*state);
  return FlovaWriteResult::accept();
}

FlovaWriteResult FlovaDevice::applyWrite(const char* key, const String& value, FlovaValueType type, FlovaValueOrigin origin, const String& commandId, const String& correlationId, uint32_t desiredVersion, bool acknowledgeCloud) {
  if (!datastreamAllowed(key)) return FlovaWriteResult::reject("unknown_datastream");
  DatastreamState* state = stateFor(key, type, true);
  if (!state || state->type != type || !valueMatchesType(value, type)) return FlovaWriteResult::reject("invalid_value");
  if (state->mode == FlovaDatastreamMode::Sample || state->mode == FlovaDatastreamMode::Event) return FlovaWriteResult::reject("not_writable");
  if (!transport_.connected() && state->offline == FlovaOfflinePolicy::Reject) return FlovaWriteResult::reject("offline_delivery_required");
  // A retained or retried older desired revision is acknowledged from current
  // state without touching hardware or replacing a newer local value.
  bool stale = desiredVersion && desiredVersion <= state->lastDesiredVersion;
  bool unchanged = state->hasValue && state->value == value;
  FlovaWriteResult result = stale || unchanged
      ? FlovaWriteResult::noChange()
      : invokeWriteHandler(*state, value, commandId, correlationId);
  bool shouldUpdate = result.accepted() && !stale && !unchanged;
  if (shouldUpdate) {
    bool dirty = !transport_.connected() && state->offline == FlovaOfflinePolicy::KeepLatest;
    updateState(*state, value, origin, dirty);
    if (transport_.connected()) publishState(*state);
  }
  if (result.accepted() && desiredVersion > state->lastDesiredVersion) state->lastDesiredVersion = desiredVersion;
  if (acknowledgeCloud) {
    String payload = "{\"command_id\":\"" + commandId + "\",\"key\":\"" + String(key) + "\",\"status\":\"" + (result.accepted() ? "ok" : "error") + "\"";
    if (result.accepted()) { payload += ",\"value\":" + jsonScalar(state->value); if (desiredVersion) payload += ",\"acknowledged_version\":" + String(desiredVersion); }
    else payload += ",\"error_code\":\"" + String(result.reasonCode) + "\",\"error_message\":\"" + String(result.message) + "\"";
    if (correlationId.length()) payload += ",\"correlation_id\":\"" + correlationId + "\"";
    payload += "}"; transport_.publish(datastreamTopic(key, result.accepted() ? "ack" : "error").c_str(), payload);
  }
  return result;
}

void FlovaDevice::setStatusLed(uint8_t pin, bool activeLow) {
  statusLedPin_ = pin;
  statusLedActiveLow_ = activeLow;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, activeLow ? HIGH : LOW);
}

void FlovaDevice::updateStatusIndicator() {
  statusIndicatorState_ = transport_.connected()
    ? FlovaStatusIndicatorState::Online
    : FlovaStatusIndicatorState::Offline;
  if (statusLedPin_ == 255) return;

  // Online is steady on; Offline blinks while MQTT reconnects.
  bool on = statusIndicatorState_ == FlovaStatusIndicatorState::Online ||
            (millis() / 500) % 2 == 0;
  digitalWrite(statusLedPin_, on == statusLedActiveLow_ ? LOW : HIGH);
}

void FlovaDevice::setFactoryResetButton(uint8_t pin, bool activeLow, uint32_t holdMs) {
  resetButtonPin_ = pin;
  resetButtonActiveLow_ = activeLow;
  resetHoldMs_ = holdMs;
  pinMode(pin, activeLow ? INPUT_PULLUP : INPUT);
}

void FlovaDevice::factoryReset() {
  storage_.clear();
  logger_.info("Factory reset requested.");
#if defined(ESP32) || defined(ESP8266)
  delay(250);
  ESP.restart();
#endif
}

void FlovaDevice::handleMessage(const String& topic, const String& payload) {
  if (topic.endsWith("/time/response")) { handleTimeSync(payload); return; }
  if (topic.endsWith("/schedules/desired")) {
    installScheduleManifest(payload);
    return;
  }
  if (topic.endsWith("/config/desired")) {
    handleConfigSet(payload);
    return;
  }
  if (topic.endsWith("/ota/desired")) { handleOtaOffer(payload); return; }
  String commandId = jsonValue("command_id", payload);
  if (!commandId.length()) commandId = jsonValue("commandId", payload);
  String correlationId = jsonValue("correlation_id", payload);
  if (!correlationId.length()) correlationId = jsonValue("correlationId", payload);
  String key = jsonValue("key", payload);
  String value = jsonValue("value", payload);
  if (!value.length()) value = jsonValue("desired_value", payload);
  String desiredVersion = jsonValue("desired_version", payload);
  String behavior = jsonValue("behavior", payload);

  if (behavior == "command" && commandSeen(commandId)) {
    acknowledgeDuplicateCommand(commandId, correlationId, key, value);
    return;
  }

  if (!datastreamAllowed(key.c_str())) {
    String error = "{\"command_id\":\"" + commandId + "\",\"key\":\"" + key + "\",\"status\":\"error\",\"error_code\":\"unknown_datastream\",\"error_message\":\"Datastream is not published for this device.\"}";
    if (correlationId.length()) error = error.substring(0, error.length() - 1) + ",\"correlation_id\":\"" + correlationId + "\"}";
    transport_.publish(datastreamTopic(key.c_str(), "error").c_str(), error);
    return;
  }

  DatastreamState* state = const_cast<DatastreamState*>(stateFor(key.c_str()));
  FlovaValueType type = state ? state->type : FlovaValueType::String;
  FlovaValueOrigin origin = behavior == "command" ? FlovaValueOrigin::UserCommand : FlovaValueOrigin::CloudAutomation;
  applyWrite(key.c_str(), value, type, origin, commandId, correlationId, (uint32_t)desiredVersion.toInt(), true);
  // Remember rejected commands too: an MQTT retry must not run a safety check
  // or hardware side effect a second time.
  if (behavior == "command") rememberCommand(commandId);
}

bool FlovaDevice::commandSeen(const String& commandId) const {
  // The ring is intentionally bounded for microcontrollers; retries inside the
  // recent window are acknowledged without executing hardware twice.
  if (!commandId.length()) return false;
  for (uint8_t i = 0; i < config_.limits.commandDedup; i++) {
    if (recentCommandIds_[i] == commandId) return true;
  }
  return false;
}

void FlovaDevice::rememberCommand(const String& commandId) {
  if (!commandId.length() || commandSeen(commandId)) return;
  recentCommandIds_[recentCommandCursor_] = commandId;
  recentCommandCursor_ = (recentCommandCursor_ + 1) % config_.limits.commandDedup;

  String stored;
  for (uint8_t i = 0; i < config_.limits.commandDedup; i++) {
    if (!recentCommandIds_[i].length()) continue;
    if (stored.length()) stored += ',';
    stored += recentCommandIds_[i];
  }
  storage_.setString("command_ids", stored.c_str());
}

void FlovaDevice::acknowledgeDuplicateCommand(const String& commandId, const String& correlationId,
                                              const String& key, const String& value) {
  const DatastreamState* state = stateFor(key.c_str());
  bool accepted = state && state->hasValue && state->value == value;
  String result = "{\"command_id\":\"" + commandId + "\",\"key\":\"" + key +
                  "\",\"status\":\"" + (accepted ? "ok" : "error") + "\"";
  if (accepted) result += ",\"value\":" + jsonScalar(state->value);
  else result += ",\"error_code\":\"duplicate_command\",\"error_message\":\"Command was already processed.\"";
  result += ",\"duplicate\":true}";
  if (correlationId.length()) {
    result = result.substring(0, result.length() - 1) + ",\"correlation_id\":\"" + correlationId + "\"}";
  }
  transport_.publish(datastreamTopic(key.c_str(), accepted ? "ack" : "error").c_str(), result);
}

void FlovaDevice::handleConfigSet(const String& payload) {
  String commandId = jsonValue("command_id", payload);
  String versionId = jsonValue("template_version_id", payload);
  String checksum = jsonValue("checksum", payload);
  bool ok = commandId.length() && versionId.length() && checksum.length() && installRuntimeConfig(payload);
  String result = "{\"command_id\":\"" + commandId + "\",\"template_version_id\":\"" + versionId +
                  "\",\"checksum\":\"" + checksum + "\",\"status\":\"" + (ok ? "ok" : "error") + "\"";
  if (!ok) result += ",\"error_message\":\"invalid_runtime_config\"";
  result += "}";
  transport_.publish(topic("config/reported").c_str(), result);
  if (ok) {
#if defined(ESP32) || defined(ESP8266)
    delay(100);
    ESP.restart();
#endif
  }
}

void FlovaDevice::handleOtaOffer(const String& payload) {
  if (!config_.otaCapable || !otaInstaller_) {
    pendingOtaPayload_ = payload;
    reportOta("failed", "ota_not_supported");
    pendingOtaPayload_ = "";
    return;
  }
  // MQTT callbacks only stage the offer. Flash and network work runs from loop().
  pendingOtaPayload_ = payload;
  reportOta("notified");
}

void FlovaDevice::processPendingOta() {
  if (!pendingOtaPayload_.length() || !otaInstaller_) return;
  DynamicJsonDocument json(2048);
  if (deserializeJson(json, pendingOtaPayload_)) {
    reportOta("failed", "invalid_manifest"); pendingOtaPayload_ = ""; return;
  }
  FlovaOtaOffer offer;
  offer.installId = String((const char*)(json["install_id"] | ""));
  offer.releaseId = String((const char*)(json["release_id"] | ""));
  offer.version = String((const char*)(json["version"] | ""));
  offer.firmwareTarget = String((const char*)(json["firmware_target"] | ""));
  offer.artifactUrl = String((const char*)(json["artifact_url"] | ""));
  offer.sha256 = String((const char*)(json["sha256"] | ""));
  offer.bootLayoutVersion = String((const char*)(json["boot_layout_version"] | ""));
  offer.sizeBytes = json["size_bytes"] | 0;
  offer.contractVersion = json["ota_contract_version"] | 0;
  offer.allowDowngrade = json["allow_downgrade"] | false;
  if (!offer.installId.length() || !offer.releaseId.length() || offer.firmwareTarget != config_.firmwareTarget ||
      offer.contractVersion != 2 || !bootControl_ || offer.bootLayoutVersion != bootControl_->layoutVersion() ||
      offer.sizeBytes == 0 || offer.sha256.length() != 64 ||
      (bootControl_->maxImageBytes() && offer.sizeBytes > bootControl_->maxImageBytes())) {
    reportOta("failed", "incompatible_manifest"); pendingOtaPayload_ = ""; return;
  }
  reportOta("installing");
  FlovaOtaResult result = otaInstaller_->install(offer);
  if (result != FlovaOtaResult::Installed) {
    const char* code = result == FlovaOtaResult::HashMismatch ? "checksum_mismatch" :
                       result == FlovaOtaResult::DownloadFailed ? "download_failed" : "flash_failed";
    reportOta("failed", code); pendingOtaPayload_ = ""; return;
  }
  storage_.setString("ota_release", offer.releaseId.c_str());
  storage_.setString("ota_install", offer.installId.c_str());
  storage_.setString("ota_version", offer.version.c_str());
  reportOta("rebooting");
  delay(100);
#if defined(ESP32) || defined(ESP8266)
  ESP.restart();
#endif
}

void FlovaDevice::reportOta(const char* status, const char* errorCode) {
  String installId = jsonValue("install_id", pendingOtaPayload_);
  String payload = "{\"protocol\":{\"name\":\"flova\",\"version\":1},\"schema_version\":1,\"message_id\":\"ota-" +
                   installId + "-" + String(clock_.millisNow()) + "\",\"install_id\":\"" + installId +
                   "\",\"status\":\"" + String(status) + "\"";
  if (errorCode) payload += ",\"error_code\":\"" + String(errorCode) + "\"";
  payload += "}";
  transport_.publish(topic("ota/reported").c_str(), payload);
}

bool FlovaDevice::handleMappedWrite(const String& commandId, const String& correlationId, const String& key, const String& value, const String& desiredVersion) {
  for (uint8_t i = 0; i < outputCount_; i++) {
    if (outputs_[i].key == key) {
      bool on = value == "true" || value == "1";
      uint32_t incomingVersion = (uint32_t)desiredVersion.toInt();
      if (incomingVersion && incomingVersion <= outputs_[i].lastAppliedDesiredVersion) {
        ackDigitalOutput(outputs_[i], commandId, correlationId);
        return true;
      }
      uint32_t now = clock_.millisNow();
      if (outputs_[i].lastAppliedMs == 0 || now - outputs_[i].lastAppliedMs >= outputs_[i].minOutputIntervalMs) {
        applyDigitalOutput(outputs_[i], on);
        outputs_[i].lastAppliedDesiredVersion = incomingVersion;
        ackDigitalOutput(outputs_[i], commandId, correlationId);
      } else {
        outputs_[i].pending = true;
        outputs_[i].pendingValue = on;
        outputs_[i].pendingCommandId = commandId;
        outputs_[i].pendingCorrelationId = correlationId;
        outputs_[i].lastAppliedDesiredVersion = incomingVersion;
      }
      return true;
    }
  }
  return false;
}

void FlovaDevice::applyDigitalOutput(DigitalOutput& output, bool value) {
  output.value = value;
  output.pending = false;
  output.lastAppliedMs = clock_.millisNow();
  digitalWrite(output.pin, value == output.activeHigh ? HIGH : LOW);
}

void FlovaDevice::ackDigitalOutput(const DigitalOutput& output, const String& commandId, const String& correlationId) {
  String result = "{\"command_id\":\"" + commandId + "\",\"key\":\"" + output.key + "\",\"status\":\"ok\",\"value\":";
  result += output.value ? "true" : "false";
  if (output.lastAppliedDesiredVersion) result += ",\"acknowledged_version\":" + String(output.lastAppliedDesiredVersion);
  if (correlationId.length()) result += ",\"correlation_id\":\"" + correlationId + "\"";
  result += "}";
  transport_.publish(datastreamTopic(output.key.c_str(), "ack").c_str(), result);
}

void FlovaDevice::flushDigitalOutputs() {
  uint32_t now = clock_.millisNow();
  for (uint8_t i = 0; i < outputCount_; i++) {
    if (outputs_[i].pending && now - outputs_[i].lastAppliedMs >= outputs_[i].minOutputIntervalMs) {
      applyDigitalOutput(outputs_[i], outputs_[i].pendingValue);
      ackDigitalOutput(outputs_[i], outputs_[i].pendingCommandId, outputs_[i].pendingCorrelationId);
      outputs_[i].pendingCommandId = "";
      outputs_[i].pendingCorrelationId = "";
    }
  }
}

void FlovaDevice::pollDigitalInputs() {
  uint32_t now = clock_.millisNow();
  for (uint8_t i = 0; i < inputCount_; i++) {
    bool raw = digitalRead(inputs_[i].pin) == (inputs_[i].activeHigh ? HIGH : LOW);
    if (raw != inputs_[i].lastRaw) {
      inputs_[i].lastRaw = raw;
      inputs_[i].changedAt = now;
    }
    if (raw != inputs_[i].lastSent && now - inputs_[i].changedAt >= inputs_[i].debounceMs) {
      inputs_[i].lastSent = raw;
      bool ok = reportValue(inputs_[i].key.c_str(), raw ? "true" : "false", FlovaValueType::Bool,
                            FlovaValueOrigin::PhysicalInput).accepted();
      String message = "digital input key=" + inputs_[i].key + " value=" + String(raw ? "true" : "false") + (ok ? " published." : " publish failed.");
      logger_.info(message.c_str());
    }
  }
}

String FlovaDevice::topic(const char* suffix) const {
  return "flova/v1/devices/" + String(config_.deviceId) + "/" + String(suffix);
}

String FlovaDevice::datastreamTopic(const char* key, const char* suffix) const {
  (void)key;
  return topic(String(suffix) == "update" ? "state" : "command-results");
}

String FlovaDevice::heartbeatPayload() const {
  const char* strategy = !bootControl_ || bootControl_->strategy() == FlovaOtaStrategy::None ? "none" :
                         bootControl_->strategy() == FlovaOtaStrategy::Ab ? "ab" : "ab_recovery";
  const char* bootState = !bootControl_ || bootControl_->state() == FlovaBootState::Stable ? "stable" :
                          bootControl_->state() == FlovaBootState::Candidate ? "candidate" :
                          bootControl_->state() == FlovaBootState::RolledBack ? "rolled_back" : "recovery";
  return "{\"protocol\":{\"name\":\"flova\",\"version\":1},\"schema_version\":1,\"capability_schema_version\":1,\"message_id\":\"heartbeat-" + String(lastHeartbeatMs_) + "\",\"status\":\"online\",\"firmware\":{\"firmwareVersion\":\"" + String(config_.firmwareVersion) +
         "\",\"firmwareTarget\":\"" + String(config_.firmwareTarget) + "\",\"runningReleaseId\":\"" + String(config_.runningReleaseId) + "\",\"lastInstallId\":\"" + String(config_.lastInstallId) +
         "\",\"otaStrategy\":\"" + String(strategy) + "\",\"bootState\":\"" + String(bootState) + "\",\"bootLayoutVersion\":\"" + String(bootControl_ ? bootControl_->layoutVersion() : "legacy") + "\",\"activeSlot\":\"" + String(bootControl_ ? bootControl_->activeSlot() : "single") + "\",\"maxImageBytes\":" + String(bootControl_ ? bootControl_->maxImageBytes() : 0) +
         ",\"rollbackReason\":\"" + String(bootControl_ ? bootControl_->rollbackReason() : "") +
         "\",\"sdkVersion\":\"" + String(config_.sdkVersion) +
         "\",\"protocolName\":\"" + String(config_.protocolName) + "\",\"protocolVersion\":" + String(config_.protocolVersion) +
         ",\"boardType\":\"" + String(config_.boardType) +
         "\",\"otaCapable\":" + String(config_.otaCapable ? "true" : "false") +
         ",\"rollbackCapable\":" + String(config_.rollbackCapable ? "true" : "false") +
         ",\"flashSizeBytes\":" + String(config_.flashSize) + "},\"capabilities\":{\"datastream_slots\":" + String(config_.capabilities.datastreamSlots) +
         ",\"hardware_input_slots\":" + String(config_.capabilities.hardwareInputSlots) + ",\"hardware_output_slots\":" + String(config_.capabilities.hardwareOutputSlots) +
         ",\"command_dedup_slots\":" + String(config_.capabilities.commandDedupSlots) + ",\"message_bytes\":" + String(config_.capabilities.messageBytes) +
         ",\"schedule_slots\":" + String(config_.capabilities.scheduleSlots) + ",\"history_bytes\":" + String(config_.capabilities.historyBytes) +
         ",\"persistent_bytes\":" + String(config_.capabilities.persistentBytes) + ",\"schedule_manifest_bytes\":" + String(config_.capabilities.scheduleManifestBytes) +
         ",\"schedule_chunks\":" + String(config_.capabilities.scheduleChunks ? "true" : "false") + "},\"limits\":{\"datastreams\":" + String(config_.limits.datastreams) +
         ",\"message_bytes\":" + String(config_.limits.messageBytes) + ",\"schedule_manifest_bytes\":" + String(config_.limits.scheduleManifestBytes) +
         ",\"schedule_renew_before_days\":" + String(config_.limits.scheduleRenewBeforeDays) + "},\"applied_template_version_id\":\"" +
         String(config_.appliedTemplateVersionId) + "\",\"config_checksum\":\"" + String(config_.configChecksum) + "\",\"schedules\":{\"revision\":" +
         String((unsigned long long)scheduleRevision_) + ",\"valid_until\":" + String((unsigned long long)scheduleValidUntil_) + "}}";
}

bool FlovaDevice::publishHeartbeat() {
  lastHeartbeatMs_ = clock_.millisNow();
  bool ok = transport_.publish(topic("heartbeat").c_str(), heartbeatPayload());
  if (ok && bootControl_ && bootControl_->state() == FlovaBootState::Candidate)
    candidateHeartbeatPublished_ = true;
  logger_.info(ok ? "MQTT heartbeat published." : "MQTT heartbeat failed.");
  return ok;
}

bool FlovaDevice::reconnect() {
  uint32_t now = clock_.millisNow();
  if (transport_.connected()) return true;
  if (now - lastReconnectMs_ < reconnectDelayMs_) return false;
  lastReconnectMs_ = now;

  logger_.info("MQTT connecting.");
  bool ok = transport_.connect(config_.deviceId, config_.mqttUsername, config_.mqttPassword);
  reconnectDelayMs_ = ok ? 1000 : min<uint32_t>(reconnectDelayMs_ * 2 + random(0, 500), 30000);
  if (!ok) {
    logger_.info("MQTT connect failed.");
    return false;
  }

  logger_.info("MQTT connected.");
  bool subscribed = transport_.subscribe(topic("commands").c_str());
  subscribed = transport_.subscribe(topic("config/desired").c_str()) && subscribed;
  subscribed = transport_.subscribe(topic("time/response").c_str()) && subscribed;
  subscribed = transport_.subscribe(topic("schedules/desired").c_str()) && subscribed;
  subscribed = transport_.subscribe(topic("ota/desired").c_str()) && subscribed;
  logger_.info(subscribed ? "MQTT write subscription ready." : "MQTT write subscription failed.");
  publishHeartbeat();
  requestTimeSync();
  return true;
}

bool FlovaDevice::installScheduleManifest(const String& payload, bool persist) {
#if !FLOVA_SCHEDULE_RUNTIME_ENABLED
  (void)payload;
  return false;
#else
  if (!payload.length() || payload.length() > config_.capabilities.scheduleManifestBytes) {
    reportScheduleStatus("manifest_too_large");
    return false;
  }
  DynamicJsonDocument document(6144);
  if (deserializeJson(document, payload)) {
    reportScheduleStatus("invalid_manifest");
    return false;
  }
  const uint64_t revision = document["revision"] | 0ULL;
  JsonArray schedules = document["schedules"].as<JsonArray>();
  if (!revision || revision <= scheduleRevision_ || schedules.isNull() || schedules.size() > FLOVA_SCHEDULE_CAPACITY) {
    reportScheduleStatus(revision == scheduleRevision_ ? "installed" : "invalid_manifest");
    return revision == scheduleRevision_;
  }
  uint8_t count = 0;
  for (JsonObject item : schedules) {
    JsonArray deltas = item["minute_deltas"].as<JsonArray>();
    const char* id = item["id"] | "";
    const char* key = item["action"]["datastream_key"] | "";
    uint64_t first = item["first_utc_ms"] | 0ULL;
    if (!id[0] || !key[0] || !first || deltas.size() >= FLOVA_SCHEDULE_OCCURRENCE_CAPACITY) {
      reportScheduleStatus("invalid_manifest");
      return false;
    }
    for (JsonVariant delta : deltas) {
      uint32_t minutes = delta.as<uint32_t>();
      if (!minutes || minutes > 65535) { reportScheduleStatus("invalid_manifest"); return false; }
    }
    count++;
  }
  if (persist && (!storage_.setString("schedule_staging", payload.c_str()) ||
                  !storage_.setString("schedule_active", payload.c_str()))) {
    reportScheduleStatus("storage_error");
    return false;
  }
  if (persist) storage_.remove("schedule_staging");
  for (uint8_t i = 0; i < FLOVA_SCHEDULE_CAPACITY; ++i) offlineSchedules_[i] = OfflineSchedule();
  count = 0;
  for (JsonObject item : schedules) {
    OfflineSchedule& parsed = offlineSchedules_[count++];
    parsed.id = String((const char*)(item["id"] | ""));
    parsed.key = String((const char*)(item["action"]["datastream_key"] | ""));
    serializeJson(item["action"]["value"], parsed.value);
    parsed.enabled = item["enabled"] | true;
    parsed.firstUtcMs = item["first_utc_ms"] | 0ULL;
    for (JsonVariant delta : item["minute_deltas"].as<JsonArray>())
      parsed.minuteDeltas[parsed.deltaCount++] = (uint16_t)delta.as<uint32_t>();
  }
  offlineScheduleCount_ = count;
  scheduleRevision_ = revision;
  scheduleValidUntil_ = document["valid_until"] | 0ULL;
  scheduleRenewBefore_ = document["renew_before"] | 0ULL;
  scheduleExpiryReported_ = false;
  reportScheduleStatus("installed");
  return true;
#endif
}

void FlovaDevice::restoreScheduleManifest() {
#if FLOVA_SCHEDULE_RUNTIME_ENABLED
  String payload;
  if (!storage_.getString("schedule_active", payload)) return;
  if (!payload.length() || payload.length() > config_.capabilities.scheduleManifestBytes) {
    storage_.remove("schedule_active");
    return;
  }
  scheduleRevision_ = 0;
  if (installScheduleManifest(payload, false)) {
    for (uint8_t i = 0; i < offlineScheduleCount_; ++i) {
      char cursor[8] = {0};
      if (storage_.getString(("schedule_progress_" + String(i)).c_str(), cursor, sizeof(cursor)))
        offlineSchedules_[i].cursor = min<uint8_t>((uint8_t)atoi(cursor), offlineSchedules_[i].deltaCount + 1);
    }
    skipPastScheduleOccurrences_ = true;
  } else storage_.remove("schedule_active");
#endif
}

void FlovaDevice::runSchedules() {
#if FLOVA_SCHEDULE_RUNTIME_ENABLED
  if (!clock_.utcValid() || !scheduleRevision_) return;
  uint64_t now = clock_.utcMillis();
  if (now >= scheduleValidUntil_) {
    if (!scheduleExpiryReported_) { reportScheduleStatus("schedule_horizon_expired"); scheduleExpiryReported_ = true; }
    requestScheduleRenewal();
    return;
  }
  if (now >= scheduleRenewBefore_) requestScheduleRenewal();
  for (uint8_t i = 0; i < offlineScheduleCount_; ++i) {
    OfflineSchedule& schedule = offlineSchedules_[i];
    uint64_t occurrence = schedule.firstUtcMs;
    for (uint8_t j = 0; j < schedule.cursor; ++j) occurrence += (uint64_t)schedule.minuteDeltas[j] * 60000ULL;
    const uint8_t total = schedule.deltaCount + 1;
    if (skipPastScheduleOccurrences_) {
      while (schedule.cursor < total && occurrence <= now) {
        schedule.cursor++;
        if (schedule.cursor < total) occurrence += (uint64_t)schedule.minuteDeltas[schedule.cursor - 1] * 60000ULL;
      }
      storage_.setString(("schedule_progress_" + String(i)).c_str(), String(schedule.cursor).c_str());
      continue;
    }
    while (schedule.enabled && schedule.cursor < total && occurrence <= now) {
      schedule.cursor++;
      storage_.setString(("schedule_progress_" + String(i)).c_str(), String(schedule.cursor).c_str());
      DatastreamState* state = stateFor(schedule.key.c_str(), FlovaValueType::String, false);
      if (state) applyWrite(schedule.key.c_str(), schedule.value, state->type, FlovaValueOrigin::Internal);
      if (schedule.cursor < total) occurrence += (uint64_t)schedule.minuteDeltas[schedule.cursor - 1] * 60000ULL;
    }
  }
  skipPastScheduleOccurrences_ = false;
#endif
}

void FlovaDevice::requestScheduleRenewal() {
  uint64_t now = clock_.utcMillis();
  if (!transport_.connected() || (lastScheduleRenewRequest_ && now - lastScheduleRenewRequest_ < 21600000ULL)) return;
  String body = "{\"revision\":" + String((unsigned long long)scheduleRevision_) + ",\"valid_until\":" + String((unsigned long long)scheduleValidUntil_) + "}";
  if (transport_.publish(topic("schedules/renew").c_str(), body)) lastScheduleRenewRequest_ = now;
}

void FlovaDevice::reportScheduleStatus(const char* status) {
  if (!transport_.connected()) return;
  String body = "{\"status\":\"" + String(status) + "\",\"revision\":" + String((unsigned long long)scheduleRevision_) + ",\"valid_until\":" + String((unsigned long long)scheduleValidUntil_) + "}";
  transport_.publish(topic("schedules/reported").c_str(), body);
}

void FlovaDevice::requestTimeSync() {
  uint32_t now = clock_.millisNow();
  if (pendingTimeRequestId_.length() && now - timeRequestStartedMs_ < 30000) return;
  pendingTimeRequestId_ = "time-" + String(now);
  String payload = "{\"protocol\":{\"name\":\"flova\",\"version\":1},\"schema_version\":1,\"message_id\":\"" + pendingTimeRequestId_ + "\",\"request_id\":\"" + pendingTimeRequestId_ + "\",\"monotonic_ms\":" + String(now) + "}";
  if (transport_.publish(topic("time/request").c_str(), payload)) { timeRequestStartedMs_ = now; lastTimeRequestMs_ = now; }
  else pendingTimeRequestId_ = "";
}

void FlovaDevice::handleTimeSync(const String& payload) {
  String requestId = jsonValue("request_id", payload); String server = jsonValue("server_utc_ms", payload);
  if (!pendingTimeRequestId_.length() || requestId != pendingTimeRequestId_ || !server.length()) return;
  uint32_t now = clock_.millisNow(); uint32_t uncertainty = (now - timeRequestStartedMs_) / 2;
  clock_.setUtc(strtoull(server.c_str(), nullptr, 10) + uncertainty, uncertainty); timeRequestStartedMs_ = 0; pendingTimeRequestId_ = "";
}

String FlovaDevice::jsonValue(const char* key, const String& payload) const {
  String needle = "\"" + String(key) + "\"";
  int keyIndex = payload.indexOf(needle);
  if (keyIndex < 0) return "";
  int colon = payload.indexOf(':', keyIndex + needle.length());
  if (colon < 0) return "";
  int start = colon + 1;
  while (start < (int)payload.length() && payload[start] == ' ') start++;
  if (payload[start] == '"') {
    int secondQuote = payload.indexOf('"', start + 1);
    if (secondQuote < 0) return "";
    return payload.substring(start + 1, secondQuote);
  }
  if (payload[start] == '{' || payload[start] == '[') {
    char open = payload[start];
    char close = open == '{' ? '}' : ']';
    int depth = 0;
    bool quoted = false;
    for (int index = start; index < (int)payload.length(); index++) {
      char current = payload[index];
      if (current == '"' && (index == 0 || payload[index - 1] != '\\')) quoted = !quoted;
      if (quoted) continue;
      if (current == open) depth++;
      if (current == close && --depth == 0) return payload.substring(start, index + 1);
    }
    return "";
  }
  int end = payload.indexOf(',', start);
  int brace = payload.indexOf('}', start);
  if (end < 0 || (brace >= 0 && brace < end)) end = brace;
  if (end < 0) end = payload.length();
  return payload.substring(start, end);
}

String FlovaDevice::jsonScalar(const String& value) const {
  if ((value.startsWith("{") && value.endsWith("}")) ||
      (value.startsWith("[") && value.endsWith("]"))) return value;
  if (value == "true" || value == "false") return value;
  bool numeric = value.length() > 0;
  for (uint16_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (!((c >= '0' && c <= '9') || c == '-' || c == '.')) numeric = false;
  }
  if (numeric) return value;
  return "\"" + value + "\"";
}

bool FlovaDevice::datastreamAllowed(const char* key) const {
  String keys = String(config_.datastreamKeys);
  if (!keys.length()) return false;
  return ("," + keys + ",").indexOf("," + String(key) + ",") >= 0;
}
