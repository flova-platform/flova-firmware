#pragma once
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <FlovaArduino.h>
#include <FlovaConfiguration.h>
#include <FlovaScheduleRuntime.h>
#include <FlovaEsp32BootControl.h>

#ifndef FLOVA_FIRMWARE_VERSION
#define FLOVA_FIRMWARE_VERSION "0.1.0"
#endif

class FlovaEsp32 : public FlovaDevice {
 public:
  FlovaEsp32() : FlovaDevice(transport_, storage_, clock_, logger_) {}

  bool begin() {
    bootControl_.begin();
    if (!loadCredentials()) {
      startProvisioningAp();
      return true;
    }
    storage_.getString("ota_release", otaReleaseId_);
    storage_.getString("ota_install", otaInstallId_);
    storage_.getString("ota_version", otaVersion_);
    bootControl_.setRolledBack(otaInstallId_.length() && otaVersion_.length() && otaVersion_ != FLOVA_FIRMWARE_VERSION);

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
    config.boardType = "esp32";
    config.firmwareTarget = "universal_esp32";
    config.runningReleaseId = otaReleaseId_.c_str();
    config.lastInstallId = otaInstallId_.c_str();
    config.otaCapable = true;
    config.rollbackCapable = bootControl_.strategy() != FlovaOtaStrategy::None;
    config.flashSize = ESP.getFlashChipSize();
    applyNegotiatedLimits(config);
    configure(config);
    setOtaInstaller(otaInstaller_);
    setBootControl(bootControl_);
    applyRuntimeMappings();
    applyRuntimeSystem();
    return FlovaDevice::begin();
  }

  void loop() {
    if (provisioning_) {
      server_.handleClient();
      return;
    }
    FlovaDevice::loop();
    if (runtimeRestartPending_) {
      Serial.println("[flova] runtime configuration stored; restarting to apply");
      delay(100);
      ESP.restart();
    }
  }

  bool installRuntimeConfig(const String& payload) override {
    DynamicJsonDocument config(8192);
    if (deserializeJson(config, payload) || !config["datastreams"].is<JsonArray>()) return false;
    flova::DeviceConfiguration next = configuration();
    if (!flova::compactRuntime(config, next.runtimeJson, next.datastreamKeys)) return false;
    next.templateVersionId = String((const char*)(config["template_version_id"] | ""));
    next.checksum = String((const char*)(config["checksum"] | ""));
    if (!storeConfiguration(next)) return false;
    runtimeRestartPending_ = true;
    return true;
  }

 private:
  bool loadCredentials() {
    String snapshot;
    flova::DeviceConfiguration config;
    if (storage_.readConfiguration(snapshot, false) && flova::decodeConfiguration(snapshot, config)) {
      applyConfiguration(config);
      return true;
    }
    if (storage_.readConfiguration(snapshot, true) && flova::decodeConfiguration(snapshot, config)) {
      applyConfiguration(config);
      storage_.writeConfiguration(snapshot);
      return true;
    }
    return false;
  }

  void startProvisioningAp() {
    provisioning_ = true;
    WiFi.mode(WIFI_AP);
    String ssid = "Flova-Setup-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    WiFi.softAP(ssid);
    server_.on("/status", HTTP_GET, [this]() { handleStatus(); });
    server_.on("/provision", HTTP_POST, [this]() { handleProvision(); });
    server_.begin();
    Serial.println("[flova] provisioning AP started: " + ssid);
    Serial.println("[flova] AP IP: " + WiFi.softAPIP().toString());
  }

  void handleStatus() {
    String chip = String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.println("[flova] GET /status");
    server_.send(200, "application/json", "{\"status\":\"setup_mode\",\"last_error\":\"" + lastProvisionError_ + "\",\"ap_ssid\":\"Flova-Setup-" + chip + "\",\"chip_id\":\"esp32-" + chip + "\",\"mac_address\":\"" + WiFi.softAPmacAddress() + "\",\"firmware_target\":\"universal_esp32\",\"protocol\":{\"name\":\"flova\",\"version\":1},\"schema_version\":1}");
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

    HTTPClient http;
    http.begin(engine + "/api/device/provision");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    String chip = String((uint32_t)ESP.getEfuseMac(), HEX);
    String payload = "{\"provisioning_token\":\"" + token + "\",\"chip_id\":\"esp32-" + chip + "\",\"mac_address\":\"" + WiFi.macAddress() + "\",\"firmware_target\":\"universal_esp32\",\"protocol\":{\"name\":\"flova\",\"version\":1},\"schema_version\":1,\"capabilities\":" + capabilityJson() + "}";
    int code = http.POST(payload);
    int responseSize = http.getSize();
    Serial.println("[flova] redeem status=" + String(code) + " body_len=" + String(responseSize));
    logHeap("before redeem parse");
    if (code <= 0) {
      http.end();
      return recoverProvisioning("redeem_failed", 502);
    }
    if (code < 200 || code >= 300) {
      recoverProvisioning(http.getStream(), 422);
      http.end();
      return;
    }
    if (responseSize < 0 || responseSize > (int)flova::kProvisioningResponseBytes) {
      http.end();
      return fail(responseSize < 0 ? "response_size_unknown" : "response_too_large");
    }
    flova::DeviceConfiguration next;
    next.wifiSsid = ssid;
    next.wifiPassword = password;
    DynamicJsonDocument filter(1024);
    flova::provisioningResponseFilter(filter);
    DynamicJsonDocument runtime(4096);
    DeserializationError runtimeError =
        deserializeJson(runtime, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (runtimeError) {
      Serial.println("[flova] runtime json failed detail=" + String(runtimeError.c_str()));
      return fail(runtimeError == DeserializationError::NoMemory ? "runtime_json_no_memory" : "runtime_json_failed");
    }
    next.deviceId = String((const char*)(runtime["device_id"] | runtime["deviceId"] | ""));
    next.mqttHost = String((const char*)(runtime["mqtt"]["host"] | ""));
    next.mqttPort = runtime["mqtt"]["port"] | 1883;
    next.mqttUsername = String((const char*)(runtime["mqtt"]["username"] | ""));
    next.mqttPassword = String((const char*)(runtime["mqtt"]["password"] | ""));
    next.templateVersionId =
        String((const char*)(runtime["template_version_id"] | runtime["device_template_version_id"] | ""));
    next.checksum = String((const char*)(runtime["checksum"] | ""));
    if (!flova::compactRuntime(runtime, next.runtimeJson, next.datastreamKeys))
      return fail("runtime_config_too_large");
    if (!storeConfiguration(next)) return recoverProvisioning("configuration_storage_failed", 500);
    lastProvisionError_ = "";
    Serial.println("[flova] stored runtime_len=" + String(next.runtimeJson.length()));
    logHeap("after configuration store");
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
    finishProvisioningFailure(status);
  }

  void recoverProvisioning(Stream& response, int status) {
    DynamicJsonDocument error(384);
    if (!deserializeJson(error, response))
      lastProvisionError_ = String((const char*)(error["error"]["message"] | error["error"]["code"] | "redeem_failed"));
    else
      lastProvisionError_ = "redeem_failed";
    finishProvisioningFailure(status);
  }

  void finishProvisioningFailure(int status) {
    if (lastProvisionError_.startsWith(":")) lastProvisionError_.remove(0, 1);
    if (lastProvisionError_.length() > 64) lastProvisionError_.remove(64);
    Serial.println("[flova] provisioning failed reason=" + lastProvisionError_);
    server_.send(status, "application/json", "{\"ok\":false,\"error\":{\"code\":\"" + lastProvisionError_ + "\",\"retryable\":true}}");
  }

  flova::DeviceConfiguration configuration() const {
    flova::DeviceConfiguration config;
    config.wifiSsid = wifiSsid_;
    config.wifiPassword = wifiPassword_;
    config.deviceId = deviceId_;
    config.mqttHost = mqttHost_;
    config.mqttPort = mqttPort_;
    config.mqttUsername = mqttUsername_;
    config.mqttPassword = mqttPassword_;
    config.datastreamKeys = datastreamKeys_;
    config.runtimeJson = runtimeJson_;
    config.templateVersionId = appliedTemplateVersionId_;
    config.checksum = configChecksum_;
    return config;
  }

  void applyConfiguration(const flova::DeviceConfiguration& config) {
    wifiSsid_ = config.wifiSsid;
    wifiPassword_ = config.wifiPassword;
    deviceId_ = config.deviceId;
    mqttHost_ = config.mqttHost;
    mqttPort_ = config.mqttPort;
    mqttUsername_ = config.mqttUsername;
    mqttPassword_ = config.mqttPassword;
    datastreamKeys_ = config.datastreamKeys;
    runtimeJson_ = config.runtimeJson;
    appliedTemplateVersionId_ = config.templateVersionId;
    configChecksum_ = config.checksum;
  }

  bool storeConfiguration(const flova::DeviceConfiguration& config) {
    String snapshot;
    if (!flova::encodeConfiguration(config, snapshot) || !storage_.writeConfiguration(snapshot)) return false;
    String verified;
    flova::DeviceConfiguration decoded;
    if (!storage_.readConfiguration(verified, false) || !flova::decodeConfiguration(verified, decoded)) return false;
    applyConfiguration(decoded);
    return true;
  }

  String capabilityJson() const {
    return "{\"datastream_slots\":" + String(FLOVA_DATASTREAM_CAPACITY) +
           ",\"hardware_input_slots\":" + String(FLOVA_HARDWARE_INPUT_CAPACITY) +
           ",\"hardware_output_slots\":" + String(FLOVA_HARDWARE_OUTPUT_CAPACITY) +
           ",\"command_dedup_slots\":" + String(FLOVA_COMMAND_DEDUP_CAPACITY) +
           ",\"schedule_slots\":" + String(FLOVA_SCHEDULE_RUNTIME_ENABLED ? FLOVA_SCHEDULE_CAPACITY : 0) +
           ",\"schedule_manifest_bytes\":" + String(FLOVA_SCHEDULE_RUNTIME_ENABLED ? 3800 : 0) +
           ",\"history_bytes\":" + String(FLOVA_HISTORY_RUNTIME_ENABLED ? FLOVA_HISTORY_CAPACITY * (FLOVA_TEXT_CAPACITY + 96UL) : 0) +
           ",\"message_bytes\":8192,\"schedule_chunks\":false}";
  }

  void applyNegotiatedLimits(FlovaConfig& config) {
    DynamicJsonDocument runtime(8192);
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
    DynamicJsonDocument runtime(8192);
    if (deserializeJson(runtime, runtimeJson_)) return;
    JsonArray streams = runtime["datastreams"].as<JsonArray>();
    for (JsonObject stream : streams) {
      JsonObject mapping = stream["hardware_mapping"];
      if (mapping.isNull()) continue;
      String key = stream["key"] | "";
      String kind = mapping["kind"] | "";
      String pinName = mapping["pin"] | "";
      if (!pinName.startsWith("GPIO")) continue;
      String active = mapping["active_level"] | "high";
      uint8_t pin = (uint8_t)pinName.substring(4).toInt();
      bool activeHigh = active != "low";
      if (kind == "digital_output") {
        addDigitalOutput(key.c_str(), pin, activeHigh, mapping["min_output_interval_ms"] | 300);
        Serial.println("[flova] output mapped key=" + key + " pin=" + pinName + " active=" + active);
      }
      if (kind == "digital_input") {
        String pull = mapping["pull"] | "none";
        uint8_t mode = pull == "pullup" ? INPUT_PULLUP : INPUT;
        addDigitalInput(key.c_str(), pin, activeHigh, mapping["debounce_ms"] | 50, mode);
        Serial.println("[flova] input mapped key=" + key + " pin=" + pinName + " active=" + active + " pull=" + pull);
      }
      if (kind == "analog_input")
        addAnalogInput(key.c_str(), pin, mapping["sample_interval_ms"] | 1000);
      if (kind == "pwm_output") {
        double minimum = stream["min_value"] | 0.0;
        double maximum = stream["max_value"] | 100.0;
        double initial = stream["default_value"]["value"] | minimum;
        addPwmOutput(key.c_str(), pin, minimum, maximum, initial);
      }
    }
  }

  void applyRuntimeSystem() {
    DynamicJsonDocument runtime(8192);
    if (deserializeJson(runtime, runtimeJson_)) return;
    JsonObject status = runtime["system"]["status_led"];
    String pinName = status["pin"] | "";
    if (pinName.startsWith("GPIO")) {
      setStatusLed((uint8_t)pinName.substring(4).toInt(), status["active_low"] | false);
      Serial.println("[flova] status LED configured pin=" + pinName +
                     " active=" + ((status["active_low"] | false) ? "low" : "high"));
    }

    JsonObject reset = runtime["system"]["factory_reset"];
    String source = reset["source"] | "";
    if (source == "gpio") {
      String resetPin = reset["pin"] | "";
      if (!resetPin.startsWith("GPIO")) return;
      bool activeLow = reset["active_low"] | true;
      setFactoryResetButton((uint8_t)resetPin.substring(4).toInt(), activeLow);
      Serial.println("[flova] factory reset input configured pin=" + resetPin +
                     " active=" + (activeLow ? "low" : "high"));
      return;
    }
    if (source != "datastream") return;
    String resetKey = reset["key"] | "";
    for (JsonObject stream : runtime["datastreams"].as<JsonArray>()) {
      JsonObject mapping = stream["hardware_mapping"];
      if (String((const char*)(stream["key"] | "")) != resetKey ||
          String((const char*)(mapping["kind"] | "")) != "digital_input")
        continue;
      String resetPin = mapping["pin"] | "";
      if (!resetPin.startsWith("GPIO")) return;
      String active = mapping["active_level"] | "high";
      String pull = mapping["pull"] | "none";
      uint8_t mode = pull == "pullup" ? INPUT_PULLUP : INPUT;
      setFactoryResetButton((uint8_t)resetPin.substring(4).toInt(), active == "low",
                            10000, mode);
      Serial.println("[flova] factory reset input configured datastream=" + resetKey);
      return;
    }
  }

  bool runtimeRestartPending_ = false;

  class Storage : public ArduinoStorage {
   public:
    Storage() { prefs_.begin("flova", false); }
    bool readConfiguration(String& out, bool backup) {
      out = prefs_.getString(backup ? "config_prev" : "config", "");
      return out.length() > 0 && out.length() < flova::kConfigurationJsonBytes;
    }
    bool writeConfiguration(const String& value) {
      if (!value.length() || value.length() >= flova::kConfigurationJsonBytes) return false;
      String current = prefs_.getString("config", "");
      if (current.length() && prefs_.putString("config_prev", current) != current.length()) return false;
      return prefs_.putString("config", value) == value.length();
    }
    bool getString(const char* key, String& out) override { out = prefs_.getString(key, ""); return out.length() > 0; }
    bool setString(const char* key, const String& value) { return prefs_.putString(key, value) > 0; }
    bool getString(const char* key, char* out, size_t maxLen) override {
      String value = prefs_.getString(key, ""); if (!value.length() || !maxLen) return false;
      value.toCharArray(out, maxLen); return true;
    }
    bool setString(const char* key, const char* value) override { return prefs_.putString(key, value) > 0; }
    bool remove(const char* key) override { return prefs_.remove(key); }
    void clear() override { prefs_.clear(); }
   private:
    Preferences prefs_;
  };

  void logHeap(const char* stage) {
    Serial.println("[flova] heap " + String(stage) + " free=" + String(ESP.getFreeHeap()) +
                   " min=" + String(ESP.getMinFreeHeap()) + " max_block=" +
                   String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  }

  ArduinoMqttTransport transport_;
  Storage storage_;
  ArduinoClock clock_;
  ArduinoLogger logger_;
  ArduinoOtaInstaller otaInstaller_;
  FlovaEsp32BootControl bootControl_;
  WebServer server_{80};
  bool provisioning_ = false;
  String lastProvisionError_;
  String wifiSsid_, wifiPassword_, deviceId_, mqttHost_, mqttUsername_, mqttPassword_, datastreamKeys_, runtimeJson_, appliedTemplateVersionId_, configChecksum_, otaReleaseId_, otaInstallId_, otaVersion_;
  uint16_t mqttPort_ = 1883;
};
