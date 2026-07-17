#pragma once
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <FlovaArduino.h>
#include <FlovaScheduleRuntime.h>

#ifndef FLOVA_FIRMWARE_VERSION
#define FLOVA_FIRMWARE_VERSION "0.1.0"
#endif
#include <LittleFS.h>

class FlovaEsp8266 : public FlovaDevice {
 public:
  FlovaEsp8266() : FlovaDevice(transport_, storage_, clock_, logger_) {}

  bool begin() {
    storage_.begin();
    if (!loadCredentials()) {
      startProvisioningAp();
      return true;
    }
    storage_.getString("config_version", appliedTemplateVersionId_);
    storage_.getString("config_checksum", configChecksum_);
    storage_.getString("ota_release", otaReleaseId_);
    storage_.getString("ota_install", otaInstallId_);

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid_.c_str(), wifiPassword_.c_str());
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
      startProvisioningAp();
      return true;
    }

    transport_.configure(mqttHost_, mqttPort_);
    FlovaConfig config;
    config.deviceId = deviceId_.c_str();
    config.firmwareVersion = FLOVA_FIRMWARE_VERSION;
    config.mqttHost = mqttHost_.c_str();
    config.mqttPort = mqttPort_;
    config.mqttUsername = mqttUsername_.c_str();
    config.mqttPassword = mqttPassword_.c_str();
    config.datastreamKeys = datastreamKeys_.c_str();
    config.appliedTemplateVersionId = appliedTemplateVersionId_.c_str();
    config.configChecksum = configChecksum_.c_str();
    config.boardType = "esp8266";
    config.firmwareTarget = "universal_esp8266";
    config.runningReleaseId = otaReleaseId_.c_str();
    config.lastInstallId = otaInstallId_.c_str();
    config.otaCapable = true;
    config.rollbackCapable = false;
    config.flashSize = ESP.getFlashChipRealSize();
    applyNegotiatedLimits(config);
    configure(config);
    setOtaInstaller(otaInstaller_);
    setBootControl(bootControl_);
    applyRuntimeSystem();
    setFactoryResetButton(0, true, 10000);
    applyRuntimeMappings();
    logHeap("before device begin");
    return FlovaDevice::begin();
  }

  void loop() {
    if (provisioning_) {
      server_.handleClient();
      return;
    }
    FlovaDevice::loop();
  }

  bool installRuntimeConfig(const String& payload) override {
    DynamicJsonDocument config(4096);
    if (deserializeJson(config, payload) || !config["datastreams"].is<JsonArray>()) return false;
    String runtime = compactRuntimeConfig(config);
    String keys = datastreamKeys(payload);
    if (!runtime.length() || runtime.length() >= 1023 || !keys.length()) return false;
    return storage_.setString("runtime", runtime) && storage_.setString("ds_keys", keys) &&
           storage_.setString("config_version", String((const char*)(config["template_version_id"] | ""))) &&
           storage_.setString("config_checksum", String((const char*)(config["checksum"] | "")));
  }

 private:
  bool loadCredentials() {
    return storage_.getString("wifi_ssid", wifiSsid_) &&
           storage_.getString("wifi_pass", wifiPassword_) &&
           storage_.getString("device_id", deviceId_) &&
           storage_.getString("mqtt_host", mqttHost_) &&
           storage_.getString("mqtt_user", mqttUsername_) &&
           storage_.getString("mqtt_pass", mqttPassword_) &&
           storage_.getString("ds_keys", datastreamKeys_) &&
           storage_.getString("runtime", runtimeJson_) &&
           storage_.getUInt("mqtt_port", mqttPort_);
  }

  void startProvisioningAp() {
    provisioning_ = true;
    WiFi.mode(WIFI_AP);
    String ssid = "Flova-Setup-" + String(ESP.getChipId(), HEX);
    WiFi.softAP(ssid);
    server_.on("/status", HTTP_GET, [this]() { handleStatus(); });
    server_.on("/provision", HTTP_POST, [this]() { handleProvision(); });
    server_.begin();
    Serial.println("[flova] provisioning AP started: " + ssid);
    Serial.println("[flova] AP IP: " + WiFi.softAPIP().toString());
  }

  void handleStatus() {
    String chip = String(ESP.getChipId(), HEX);
    Serial.println("[flova] GET /status");
    server_.send(200, "application/json", "{\"status\":\"setup_mode\",\"last_error\":\"" + lastProvisionError_ + "\",\"ap_ssid\":\"Flova-Setup-" + chip + "\",\"chip_id\":\"esp8266-" + chip + "\",\"mac_address\":\"" + WiFi.softAPmacAddress() + "\",\"firmware_target\":\"universal_esp8266\",\"protocol\":{\"name\":\"flova\",\"version\":1},\"schema_version\":1}");
  }

  void handleProvision() {
    String body = server_.arg("plain");
    Serial.println("[flova] POST /provision body_len=" + String(body.length()));
    Serial.println("[flova] content_type=" + server_.header("Content-Type"));
    DynamicJsonDocument request(1024);
    DeserializationError jsonError = deserializeJson(request, body);
    if (jsonError) {
      Serial.println("[flova] invalid_json detail=" + String(jsonError.c_str()));
      return fail("invalid_json", jsonError.c_str());
    }
    String ssid = request["wifi"]["ssid"] | "";
    String password = request["wifi"]["password"] | "";
    String token = request["flova"]["provisioning_token"] | "";
    String engine = request["flova"]["api_base_url"] | "";
    Serial.println("[flova] fields ssid_len=" + String(ssid.length()) + " pass_len=" + String(password.length()) + " token_len=" + String(token.length()) + " engine=" + engine);
    if (ssid.length() == 0 || token.length() == 0 || engine.length() == 0) return fail("missing_fields");

    // Keep the setup endpoint reachable until the final result is returned.
    WiFi.mode(WIFI_AP_STA);
    Serial.println("[flova] connecting wifi ssid=" + ssid);
    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[flova] wifi_failed status=" + String(WiFi.status()));
      return recoverProvisioning("wifi_failed", 422);
    }
    Serial.println("[flova] wifi connected ip=" + WiFi.localIP().toString());

    WiFiClient client;
    HTTPClient http;
    http.begin(client, engine + "/api/device/provision");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    String chip = String(ESP.getChipId(), HEX);
    String payload = "{\"provisioning_token\":\"" + token + "\",\"chip_id\":\"esp8266-" + chip + "\",\"mac_address\":\"" + WiFi.macAddress() + "\",\"firmware_target\":\"universal_esp8266\",\"protocol\":{\"name\":\"flova\",\"version\":1},\"schema_version\":1,\"capabilities\":" + capabilityJson() + "}";
    int code = http.POST(payload);
    String response = http.getString();
    http.end();
    Serial.println("[flova] redeem status=" + String(code) + " body_len=" + String(response.length()));
    if (code < 200 || code >= 300) return recoverProvisioning(response, code <= 0 ? 502 : 422);
    DynamicJsonDocument runtime(4096);
    if (deserializeJson(runtime, response)) return fail("runtime_json_failed");
    String compactRuntime = compactRuntimeConfig(runtime);
    if (compactRuntime.length() >= 1023) return fail("runtime_config_too_large");
    String deviceId = String((const char*)(runtime["device_id"] | runtime["deviceId"] | ""));
    String mqttHost = String((const char*)(runtime["mqtt"]["host"] | ""));
    String mqttUser = String((const char*)(runtime["mqtt"]["username"] | ""));
    String mqttPass = String((const char*)(runtime["mqtt"]["password"] | ""));
    String keys = datastreamKeys(response);
    if (!deviceId.length() || !mqttHost.length() || !mqttUser.length() || !mqttPass.length() || !keys.length())
      return recoverProvisioning("invalid_engine_response", 502);
    bool stored = storage_.setString("wifi_ssid", ssid) && storage_.setString("wifi_pass", password) &&
                  storage_.setString("device_id", deviceId) && storage_.setString("mqtt_host", mqttHost) &&
                  storage_.setUInt("mqtt_port", runtime["mqtt"]["port"] | 1883) &&
                  storage_.setString("mqtt_user", mqttUser) && storage_.setString("mqtt_pass", mqttPass) &&
                  storage_.setString("ds_keys", keys) && storage_.setString("runtime", compactRuntime);
    if (!stored) return recoverProvisioning("configuration_storage_failed", 500);
    lastProvisionError_ = "";
    Serial.println("[flova] stored runtime_len=" + String(compactRuntime.length()));
    server_.send(200, "application/json", "{\"ok\":true,\"status\":\"provisioned\"}");
    delay(500);
    ESP.restart();
  }

  void fail(const char* reason) { server_.send(422, "application/json", "{\"ok\":false,\"error\":\"" + String(reason) + "\"}"); }
  void fail(const char* reason, const char* detail) { server_.send(422, "application/json", "{\"ok\":false,\"error\":\"" + String(reason) + "\",\"detail\":\"" + String(detail) + "\"}"); }

  void recoverProvisioning(const String& response, int status) {
    DynamicJsonDocument error(384);
    if (!deserializeJson(error, response))
      lastProvisionError_ = String((const char*)(error["error"]["message"] | error["error"]["code"] | "redeem_failed"));
    else
      lastProvisionError_ = response == "wifi_failed" ? "wifi_failed" : "redeem_failed";
    if (lastProvisionError_.startsWith(":")) lastProvisionError_.remove(0, 1);
    if (lastProvisionError_.length() > 64) lastProvisionError_.remove(64);
    Serial.println("[flova] provisioning failed reason=" + lastProvisionError_);
    server_.send(status, "application/json", "{\"ok\":false,\"error\":{\"code\":\"" + lastProvisionError_ + "\",\"retryable\":true}}");
  }

  String jsonValue(const String& payload, const char* key) {
    String needle = "\"" + String(key) + "\"";
    int pos = payload.indexOf(needle);
    int colon = payload.indexOf(':', pos + needle.length());
    int first = payload.indexOf('"', colon + 1);
    int second = payload.indexOf('"', first + 1);
    if (pos < 0 || colon < 0 || first < 0 || second < 0) return "";
    return payload.substring(first + 1, second);
  }

  uint16_t jsonUInt(const String& payload, const char* key, uint16_t fallback) {
    String needle = "\"" + String(key) + "\"";
    int pos = payload.indexOf(needle);
    int colon = payload.indexOf(':', pos + needle.length());
    if (pos < 0 || colon < 0) return fallback;
    return payload.substring(colon + 1).toInt();
  }

  String jsonObjectValue(const String& payload, const char* objectKey, const char* key) {
    return jsonValue(jsonObject(payload, objectKey), key);
  }

  uint16_t jsonObjectUInt(const String& payload, const char* objectKey, const char* key, uint16_t fallback) {
    return jsonUInt(jsonObject(payload, objectKey), key, fallback);
  }

  String jsonObject(const String& payload, const char* objectKey) {
    String needle = "\"" + String(objectKey) + "\"";
    int pos = payload.indexOf(needle);
    int start = payload.indexOf('{', pos + needle.length());
    if (pos < 0 || start < 0) return "";
    int depth = 0;
    for (int i = start; i < (int)payload.length(); i++) {
      if (payload[i] == '{') depth++;
      if (payload[i] == '}') depth--;
      if (depth == 0) return payload.substring(start, i + 1);
    }
    return "";
  }

  String datastreamKeys(const String& payload) {
    int pos = payload.indexOf("\"datastreams\"");
    int start = payload.indexOf('[', pos);
    int end = payload.indexOf(']', start);
    String keys;
    while (start >= 0 && end > start) {
      int keyPos = payload.indexOf("\"key\"", start);
      if (keyPos < 0 || keyPos > end) break;
      String key = jsonValue(payload.substring(keyPos, end), "key");
      if (key.length()) keys += (keys.length() ? "," : "") + key;
      start = keyPos + 5;
    }
    return keys;
  }

  String compactRuntimeConfig(JsonDocument& runtime) {
    DynamicJsonDocument compact(1024);
    compact["limits"] = runtime["limits"];
    JsonObject system = runtime["firmware_system"];
    if (system.isNull()) system = runtime["system"];
    if (!system.isNull()) compact["system"] = system;
    JsonArray out = compact.createNestedArray("datastreams");
    for (JsonObject stream : runtime["datastreams"].as<JsonArray>()) {
      JsonObject mapping = stream["hardware_mapping"];
      if (mapping.isNull()) continue;
      JsonObject row = out.createNestedObject();
      row["key"] = stream["key"] | "";
      JsonObject mapped = row.createNestedObject("hardware_mapping");
      mapped["kind"] = mapping["kind"] | "";
      mapped["pin"] = mapping["pin"] | "";
      mapped["active_level"] = mapping["active_level"] | "high";
      mapped["pull"] = mapping["pull"] | "none";
      mapped["debounce_ms"] = mapping["debounce_ms"] | 50;
      mapped["min_output_interval_ms"] = mapping["min_output_interval_ms"] | 300;
    }
    String outJson;
    serializeJson(compact, outJson);
    return outJson;
  }

  String capabilityJson() const {
    return "{\"datastream_slots\":" + String(FLOVA_DATASTREAM_CAPACITY) +
           ",\"hardware_input_slots\":" + String(FLOVA_HARDWARE_INPUT_CAPACITY) +
           ",\"hardware_output_slots\":" + String(FLOVA_HARDWARE_OUTPUT_CAPACITY) +
           ",\"command_dedup_slots\":" + String(FLOVA_COMMAND_DEDUP_CAPACITY) +
           ",\"schedule_slots\":" + String(FLOVA_SCHEDULE_RUNTIME_ENABLED ? FLOVA_SCHEDULE_CAPACITY : 0) +
           ",\"schedule_manifest_bytes\":" + String(FLOVA_SCHEDULE_RUNTIME_ENABLED ? 3800 : 0) +
           ",\"history_bytes\":" + String(FLOVA_HISTORY_RUNTIME_ENABLED ? FLOVA_HISTORY_CAPACITY * (FLOVA_TEXT_CAPACITY + 96UL) : 0) +
           ",\"message_bytes\":4096,\"schedule_chunks\":false}";
  }

  void applyNegotiatedLimits(FlovaConfig& config) {
    DynamicJsonDocument runtime(4096);
    if (deserializeJson(runtime, runtimeJson_)) return;
    JsonObject limits = runtime["limits"];
    config.limits.datastreams = limits["datastreams"] | 0;
    config.limits.hardwareInputs = limits["hardware_inputs"] | 0;
    config.limits.hardwareOutputs = limits["hardware_outputs"] | 0;
    config.limits.commandDedup = limits["command_dedup"] | 0;
    config.limits.messageBytes = limits["message_bytes"] | 0;
    config.limits.scheduleManifestBytes = limits["manifest_bytes"] | 0;
    config.limits.scheduleRenewBeforeDays = limits["renew_before_days"] | 0;
  }

  void applyRuntimeMappings() {
    DynamicJsonDocument runtime(4096);
    if (deserializeJson(runtime, runtimeJson_)) return;
    JsonArray streams = runtime["datastreams"].as<JsonArray>();
    for (JsonObject stream : streams) {
      JsonObject mapping = stream["hardware_mapping"];
      if (mapping.isNull()) continue;
      String key = stream["key"] | "";
      String kind = mapping["kind"] | "";
      String pinName = mapping["pin"] | "";
      String active = mapping["active_level"] | "high";
      uint8_t pin = (uint8_t)pinName.substring(4).toInt();
      bool activeHigh = active != "low";
      if (kind == "digital_output") addDigitalOutput(key.c_str(), pin, activeHigh, mapping["min_output_interval_ms"] | 300);
      if (kind == "digital_input") {
        String pull = mapping["pull"] | "none";
        uint8_t mode = pull == "pullup" ? INPUT_PULLUP : INPUT;
        addDigitalInput(key.c_str(), pin, activeHigh, mapping["debounce_ms"] | 50, mode);
        Serial.println("[flova] input mapped key=" + key + " pin=" + pinName + " active=" + active + " pull=" + pull);
      }
    }
  }

  void applyRuntimeSystem() {
    DynamicJsonDocument runtime(4096);
    if (deserializeJson(runtime, runtimeJson_)) return;
    JsonObject status = runtime["system"]["status_led"];
    String pinName = status["pin"] | "";
    if (!pinName.startsWith("GPIO")) return;
    setStatusLed((uint8_t)pinName.substring(4).toInt(), status["active_low"] | false);
  }

  class Storage : public ArduinoStorage {
   public:
    void begin() { EEPROM.begin(4096); LittleFS.begin(); }
    bool getString(const char* key, String& out) override { if (scheduleKey(key)) return readFile(key, out); out = read(slot(key)); return out.length() > 0; }
    bool setString(const char* key, const String& value) { if (scheduleKey(key)) return writeFile(key, value); write(slot(key), value); return true; }
    bool getString(const char* key, char* out, size_t maxLen) override {
      String value; if (!getString(key, value) || !maxLen) return false;
      value.toCharArray(out, maxLen); return true;
    }
    bool setString(const char* key, const char* value) override { return setString(key, String(value)); }
    bool remove(const char* key) override { if (scheduleKey(key)) return LittleFS.remove(path(key)); write(slot(key), ""); return true; }
    bool getUInt(const char* key, uint16_t& out) { String value = read(slot(key)); out = value.toInt(); return out > 0; }
    bool setUInt(const char* key, uint16_t value) { write(slot(key), String(value)); return true; }
    void clear() override { for (int i = 0; i < 4096; i++) EEPROM.write(i, 0); EEPROM.commit(); LittleFS.format(); }
   private:
    bool scheduleKey(const char* key) { String k(key); return k.startsWith("schedule_") || k.startsWith("ota_"); }
    String path(const char* key) { return "/" + String(key) + ".json"; }
    bool readFile(const char* key, String& out) { File file = LittleFS.open(path(key), "r"); if (!file) return false; out = file.readString(); file.close(); return out.length() > 0; }
    bool writeFile(const char* key, const String& value) { String target = path(key); String output = String(key) == "schedule_active" ? target + ".tmp" : target; File file = LittleFS.open(output, "w"); if (!file) return false; size_t written = file.print(value); file.close(); if (written != value.length()) return false; if (output != target) { LittleFS.remove(target); return LittleFS.rename(output, target); } return true; }
    int slot(const char* key) {
      String k(key);
      if (k == "wifi_ssid") return 0;
      if (k == "wifi_pass") return 96;
      if (k == "device_id") return 192;
      if (k == "mqtt_host") return 288;
      if (k == "mqtt_user") return 384;
      if (k == "mqtt_pass") return 480;
      if (k == "mqtt_port") return 576;
      if (k == "ds_keys") return 672;
      if (k == "runtime") return 768;
      if (k == "config_version") return 1792;
      if (k == "config_checksum") return 1888;
      if (k == "command_ids") return 1984;
      if (k.startsWith("ds:")) {
        uint16_t hash = 5381;
        for (uint16_t i = 0; i < k.length(); ++i) hash = ((hash << 5) + hash) ^ k[i];
        return 2208 + (hash % 8) * 224;
      }
      return 1984;
    }
    int size(int offset) { if (offset == 768) return 1024; if (offset >= 2208) return 224; if (offset == 1984) return 224; return 96; }
    String read(int offset) { int len = size(offset); char buf[len]; for (int i = 0; i < len - 1; i++) { buf[i] = EEPROM.read(offset + i); if (buf[i] == 0) break; } buf[len - 1] = 0; return String(buf); }
    void write(int offset, const String& value) { int len = size(offset); for (int i = 0; i < len - 1; i++) EEPROM.write(offset + i, i < (int)value.length() ? value[i] : 0); EEPROM.commit(); }
  };

  void logHeap(const char* stage) {
    Serial.println("[flova] heap " + String(stage) + " free=" + String(ESP.getFreeHeap()) +
                   " max_block=" + String(ESP.getMaxFreeBlockSize()) +
                   " fragmentation=" + String(ESP.getHeapFragmentation()) + "%");
  }

  ArduinoMqttTransport transport_;
  Storage storage_;
  ArduinoClock clock_;
  ArduinoLogger logger_;
  ArduinoOtaInstaller otaInstaller_;
  FlovaLegacyBootControl bootControl_;
  ESP8266WebServer server_{80};
  bool provisioning_ = false;
  String lastProvisionError_;
  String wifiSsid_, wifiPassword_, deviceId_, mqttHost_, mqttUsername_, mqttPassword_, datastreamKeys_, runtimeJson_, appliedTemplateVersionId_, configChecksum_, otaReleaseId_, otaInstallId_;
  uint16_t mqttPort_ = 1883;
};
