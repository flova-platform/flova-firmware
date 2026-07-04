#pragma once
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <FlovaArduino.h>

class FlovaEsp8266 : public FlovaDevice {
 public:
  FlovaEsp8266() : FlovaDevice(transport_, storage_, clock_, logger_) {}

  bool begin() {
    storage_.begin();
    if (!loadCredentials()) {
      startProvisioningAp();
      return true;
    }

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
    config.mqttHost = mqttHost_.c_str();
    config.mqttPort = mqttPort_;
    config.mqttUsername = mqttUsername_.c_str();
    config.mqttPassword = mqttPassword_.c_str();
    config.datastreamKeys = datastreamKeys_.c_str();
    config.boardType = "esp8266";
    config.otaCapable = true;
    config.rollbackCapable = false;
    config.flashSize = ESP.getFlashChipRealSize();
    configure(config);
    setStatusLed(2, true);
    setFactoryResetButton(0, true, 10000);
    applyRuntimeMappings();
    return FlovaDevice::begin();
  }

  void loop() {
    if (provisioning_) {
      server_.handleClient();
      return;
    }
    FlovaDevice::loop();
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
    server_.send(200, "application/json", "{\"status\":\"setup_mode\",\"ap_ssid\":\"Flova-Setup-" + chip + "\",\"chip_id\":\"esp8266-" + chip + "\",\"mac_address\":\"" + WiFi.softAPmacAddress() + "\",\"firmware_target\":\"universal_esp8266\",\"protocol_version\":\"datastream-v1\"}");
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

    server_.send(202, "application/json", "{\"ok\":true,\"stage\":\"accepted\"}");
    delay(250);
    WiFi.mode(WIFI_STA);
    Serial.println("[flova] connecting wifi ssid=" + ssid);
    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[flova] wifi_failed status=" + String(WiFi.status()));
      return fail("wifi_failed");
    }
    Serial.println("[flova] wifi connected ip=" + WiFi.localIP().toString());

    WiFiClient client;
    HTTPClient http;
    http.begin(client, engine + "/api/device/provision");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    String chip = String(ESP.getChipId(), HEX);
    String payload = "{\"provisioning_token\":\"" + token + "\",\"chip_id\":\"esp8266-" + chip + "\",\"mac_address\":\"" + WiFi.macAddress() + "\",\"firmware_target\":\"universal_esp8266\",\"protocol_version\":\"datastream-v1\"}";
    int code = http.POST(payload);
    String response = http.getString();
    http.end();
    Serial.println("[flova] redeem status=" + String(code) + " body_len=" + String(response.length()));
    Serial.println("[flova] redeem body=" + response);
    if (code < 200 || code >= 300) return fail("redeem_failed");
    DynamicJsonDocument runtime(4096);
    if (deserializeJson(runtime, response)) return fail("runtime_json_failed");
    String compactRuntime = compactRuntimeConfig(runtime);
    if (compactRuntime.length() >= 1023) return fail("runtime_config_too_large");

    storage_.setString("wifi_ssid", ssid);
    storage_.setString("wifi_pass", password);
    storage_.setString("device_id", String((const char*)(runtime["device_id"] | runtime["deviceId"] | "")));
    storage_.setString("mqtt_host", String((const char*)(runtime["mqtt"]["host"] | "")));
    storage_.setUInt("mqtt_port", runtime["mqtt"]["port"] | 1883);
    storage_.setString("mqtt_user", String((const char*)(runtime["mqtt"]["username"] | "")));
    storage_.setString("mqtt_pass", String((const char*)(runtime["mqtt"]["password"] | "")));
    storage_.setString("ds_keys", datastreamKeys(response));
    storage_.setString("runtime", compactRuntime);
    Serial.println("[flova] stored runtime_len=" + String(compactRuntime.length()));
    delay(500);
    ESP.restart();
  }

  void fail(const char* reason) { server_.send(422, "application/json", "{\"ok\":false,\"error\":\"" + String(reason) + "\"}"); }
  void fail(const char* reason, const char* detail) { server_.send(422, "application/json", "{\"ok\":false,\"error\":\"" + String(reason) + "\",\"detail\":\"" + String(detail) + "\"}"); }

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
    }
    String outJson;
    serializeJson(compact, outJson);
    return outJson;
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
      if (kind == "digital_output") addDigitalOutput(key.c_str(), pin, activeHigh);
      if (kind == "digital_input") {
        String pull = mapping["pull"] | "none";
        uint8_t mode = pull == "pullup" ? INPUT_PULLUP : INPUT;
        addDigitalInput(key.c_str(), pin, activeHigh, mapping["debounce_ms"] | 50, mode);
        Serial.println("[flova] input mapped key=" + key + " pin=" + pinName + " active=" + active + " pull=" + pull);
      }
    }
  }

  class Storage : public ArduinoStorage {
   public:
    void begin() { EEPROM.begin(2048); }
    bool getString(const char* key, String& out) { out = read(slot(key)); return out.length() > 0; }
    bool setString(const char* key, const String& value) { write(slot(key), value); return true; }
    bool getUInt(const char* key, uint16_t& out) { String value = read(slot(key)); out = value.toInt(); return out > 0; }
    bool setUInt(const char* key, uint16_t value) { write(slot(key), String(value)); return true; }
    void clear() override { for (int i = 0; i < 2048; i++) EEPROM.write(i, 0); EEPROM.commit(); }
   private:
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
      return 1888;
    }
    int size(int offset) { return offset == 768 ? 1024 : 96; }
    String read(int offset) { int len = size(offset); char buf[len]; for (int i = 0; i < len - 1; i++) { buf[i] = EEPROM.read(offset + i); if (buf[i] == 0) break; } buf[len - 1] = 0; return String(buf); }
    void write(int offset, const String& value) { int len = size(offset); for (int i = 0; i < len - 1; i++) EEPROM.write(offset + i, i < (int)value.length() ? value[i] : 0); EEPROM.commit(); }
  };

  ArduinoMqttTransport transport_;
  Storage storage_;
  ArduinoClock clock_;
  ArduinoLogger logger_;
  ESP8266WebServer server_{80};
  bool provisioning_ = false;
  String wifiSsid_, wifiPassword_, deviceId_, mqttHost_, mqttUsername_, mqttPassword_, datastreamKeys_, runtimeJson_;
  uint16_t mqttPort_ = 1883;
};
