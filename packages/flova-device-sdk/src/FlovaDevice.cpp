#include "FlovaDevice.h"

static FlovaDevice* activeDevice = nullptr;

static void dispatchMessage(const String& topic, const String& payload) {
  if (activeDevice) activeDevice->handleMessage(topic, payload);
}

void FlovaDevice::configure(const FlovaConfig& config) { config_ = config; }

bool FlovaDevice::begin() {
  activeDevice = this;
  transport_.setCallback(dispatchMessage);
  return transport_.begin() && reconnect();
}

void FlovaDevice::loop() {
  transport_.loop();
  if (!transport_.connected()) reconnect();
  if (statusLedPin_ != 255) {
    digitalWrite(statusLedPin_, transport_.connected() ? (statusLedActiveLow_ ? LOW : HIGH) : (statusLedActiveLow_ ? HIGH : LOW));
  }
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
  if (transport_.connected()) pollDigitalInputs();
  flushDigitalOutputs();
}

void FlovaDevice::onWrite(const char* key, FlovaWriteHandler handler) {
  if (handlerCount_ >= 8) return;
  handlers_[handlerCount_].key = String(key);
  handlers_[handlerCount_].handler = handler;
  handlerCount_++;
}

void FlovaDevice::addDigitalOutput(const char* key, uint8_t pin, bool activeHigh, uint32_t minOutputIntervalMs) {
  if (outputCount_ >= 8) return;
  outputs_[outputCount_].key = String(key);
  outputs_[outputCount_].pin = pin;
  outputs_[outputCount_].activeHigh = activeHigh;
  outputs_[outputCount_].minOutputIntervalMs = minOutputIntervalMs;
  outputCount_++;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, activeHigh ? LOW : HIGH);
}

void FlovaDevice::addDigitalInput(const char* key, uint8_t pin, bool activeHigh, uint32_t debounceMs, uint8_t mode) {
  if (inputCount_ >= 8) return;
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

bool FlovaDevice::write(const char* key, bool value) {
  if (!transport_.connected()) return false;
  if (!datastreamAllowed(key)) return false;
  String payload = "{\"key\":\"" + String(key) + "\",\"value\":" + String(value ? "true" : "false") + "}";
  return transport_.publish(datastreamTopic(key, "update").c_str(), payload);
}

bool FlovaDevice::write(const char* key, double value) {
  if (!transport_.connected()) return false;
  if (!datastreamAllowed(key)) return false;
  String payload = "{\"key\":\"" + String(key) + "\",\"value\":" + String(value) + "}";
  return transport_.publish(datastreamTopic(key, "update").c_str(), payload);
}

bool FlovaDevice::write(const char* key, const char* value) {
  if (!transport_.connected()) return false;
  if (!datastreamAllowed(key)) return false;
  String payload = "{\"key\":\"" + String(key) + "\",\"value\":\"" + String(value) + "\"}";
  return transport_.publish(datastreamTopic(key, "update").c_str(), payload);
}

void FlovaDevice::setStatusLed(uint8_t pin, bool activeLow) {
  statusLedPin_ = pin;
  statusLedActiveLow_ = activeLow;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, activeLow ? HIGH : LOW);
}

void FlovaDevice::setFactoryResetButton(uint8_t pin, bool activeLow, uint32_t holdMs) {
  resetButtonPin_ = pin;
  resetButtonActiveLow_ = activeLow;
  resetHoldMs_ = holdMs;
  pinMode(pin, activeLow ? INPUT_PULLUP : INPUT);
}

bool FlovaDevice::ack(const String& commandId, const String& status, const String& resultJson) {
  String key = jsonValue("key", resultJson);
  String value = jsonValue("value", resultJson);
  String payload = "{\"command_id\":\"" + commandId + "\",\"key\":\"" + key + "\",\"status\":\"" + status + "\"";
  if (status == "ok") payload += ",\"value\":" + jsonScalar(value);
  else payload += ",\"error_code\":\"handler_failed\",\"error_message\":" + jsonScalar(jsonValue("error", resultJson));
  String ts = clock_.isoNow();
  if (ts.length()) payload += ",\"timestamp\":\"" + ts + "\"";
  payload += "}";
  return transport_.publish(datastreamTopic(key.c_str(), status == "ok" ? "ack" : "error").c_str(), payload);
}

void FlovaDevice::factoryReset() {
  storage_.clear();
  logger_.info("Factory reset requested.");
#if defined(ESP32) || defined(ESP8266)
  delay(250);
  ESP.restart();
#endif
}

void FlovaDevice::handleMessage(const String&, const String& payload) {
  String commandId = jsonValue("command_id", payload);
  if (!commandId.length()) commandId = jsonValue("commandId", payload);
  String correlationId = jsonValue("correlation_id", payload);
  if (!correlationId.length()) correlationId = jsonValue("correlationId", payload);
  String key = jsonValue("key", payload);
  String value = jsonValue("value", payload);
  if (!value.length()) value = jsonValue("desired_value", payload);
  String desiredVersion = jsonValue("desired_version", payload);

  if (!datastreamAllowed(key.c_str())) {
    String error = "{\"command_id\":\"" + commandId + "\",\"key\":\"" + key + "\",\"status\":\"error\",\"error_code\":\"unknown_datastream\",\"error_message\":\"Datastream is not published for this device.\"}";
    if (correlationId.length()) error = error.substring(0, error.length() - 1) + ",\"correlation_id\":\"" + correlationId + "\"}";
    transport_.publish(datastreamTopic(key.c_str(), "error").c_str(), error);
    return;
  }

  if (handleMappedWrite(commandId, correlationId, key, value, desiredVersion)) return;

  for (uint8_t i = 0; i < handlerCount_; i++) {
    if (handlers_[i].key == key && handlers_[i].handler) {
      bool ok = handlers_[i].handler(FlovaValue(value));
      String result = "{\"command_id\":\"" + commandId + "\",\"key\":\"" + key + "\",\"status\":\"";
      result += ok ? "ok" : "error";
      result += "\",\"value\":" + jsonScalar(value) + "}";
      if (desiredVersion.length()) result = result.substring(0, result.length() - 1) + ",\"acknowledged_version\":" + desiredVersion + "}";
      if (correlationId.length()) result = result.substring(0, result.length() - 1) + ",\"correlation_id\":\"" + correlationId + "\"}";
      transport_.publish(datastreamTopic(key.c_str(), ok ? "ack" : "error").c_str(), result);
      return;
    }
  }

  String error = "{\"command_id\":\"" + commandId + "\",\"key\":\"" + key + "\",\"status\":\"error\",\"error_code\":\"unknown_datastream\",\"error_message\":\"No handler registered.\"}";
  if (correlationId.length()) error = error.substring(0, error.length() - 1) + ",\"correlation_id\":\"" + correlationId + "\"}";
  transport_.publish(datastreamTopic(key.c_str(), "error").c_str(), error);
}

bool FlovaDevice::handleMappedWrite(const String& commandId, const String& correlationId, const String& key, const String& value, const String& desiredVersion) {
  for (uint8_t i = 0; i < outputCount_; i++) {
    if (outputs_[i].key == key) {
      bool on = value == "true" || value == "1";
      uint32_t incomingVersion = (uint32_t)desiredVersion.toInt();
      if (incomingVersion && incomingVersion <= outputs_[i].lastAppliedDesiredVersion) {
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
      bool ok = write(inputs_[i].key.c_str(), raw);
      String message = "digital input key=" + inputs_[i].key + " value=" + String(raw ? "true" : "false") + (ok ? " published." : " publish failed.");
      logger_.info(message.c_str());
    }
  }
}

String FlovaDevice::topic(const char* suffix) const {
  return "devices/" + String(config_.deviceId) + "/" + String(suffix);
}

String FlovaDevice::datastreamTopic(const char* key, const char* suffix) const {
  return "devices/" + String(config_.deviceId) + "/ds/" + String(key) + "/" + String(suffix);
}

String FlovaDevice::heartbeatPayload() const {
  return "{\"status\":\"online\",\"firmware\":{\"firmwareVersion\":\"" + String(config_.firmwareVersion) +
         "\",\"sdkVersion\":\"" + String(config_.sdkVersion) +
         "\",\"protocolVersion\":\"" + String(config_.protocolVersion) +
         "\",\"boardType\":\"" + String(config_.boardType) +
         "\",\"otaCapable\":" + String(config_.otaCapable ? "true" : "false") +
         ",\"rollbackCapable\":" + String(config_.rollbackCapable ? "true" : "false") +
         ",\"flashSizeBytes\":" + String(config_.flashSize) + "}}";
}

bool FlovaDevice::publishHeartbeat() {
  lastHeartbeatMs_ = clock_.millisNow();
  bool ok = transport_.publish(topic("heartbeat").c_str(), heartbeatPayload());
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
  reconnectDelayMs_ = ok ? 1000 : min<uint32_t>(reconnectDelayMs_ * 2, 30000);
  if (!ok) {
    logger_.info("MQTT connect failed.");
    return false;
  }

  logger_.info("MQTT connected.");
  bool subscribed = transport_.subscribe(("devices/" + String(config_.deviceId) + "/ds/+/write").c_str());
  logger_.info(subscribed ? "MQTT write subscription ready." : "MQTT write subscription failed.");
  publishHeartbeat();
  return true;
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
  int end = payload.indexOf(',', start);
  int brace = payload.indexOf('}', start);
  if (end < 0 || (brace >= 0 && brace < end)) end = brace;
  if (end < 0) end = payload.length();
  return payload.substring(start, end);
}

String FlovaDevice::jsonScalar(const String& value) const {
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
