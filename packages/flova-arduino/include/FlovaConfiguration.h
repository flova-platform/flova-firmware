#pragma once

#include <ArduinoJson.h>

namespace flova {

static const size_t kRuntimeJsonBytes = 2048;
static const size_t kConfigurationJsonBytes = 4096;
static const size_t kProvisioningResponseBytes = 16384;

struct DeviceConfiguration {
  String wifiSsid;
  String wifiPassword;
  String deviceId;
  String mqttHost;
  String mqttUsername;
  String mqttPassword;
  String datastreamKeys;
  String runtimeJson;
  String templateVersionId;
  String checksum;
  uint16_t mqttPort = 1883;
};

inline void provisioningResponseFilter(JsonDocument& filter) {
  filter["device_id"] = true;
  filter["deviceId"] = true;
  filter["template_version_id"] = true;
  filter["device_template_version_id"] = true;
  filter["checksum"] = true;
  filter["mqtt"]["host"] = true;
  filter["mqtt"]["port"] = true;
  filter["mqtt"]["username"] = true;
  filter["mqtt"]["password"] = true;
  filter["limits"] = true;
  filter["firmware_system"] = true;
  filter["system"] = true;
  filter["datastreams"][0]["key"] = true;
  filter["datastreams"][0]["min_value"] = true;
  filter["datastreams"][0]["max_value"] = true;
  filter["datastreams"][0]["default_value"] = true;
  filter["datastreams"][0]["hardware_mapping"] = true;
}

inline bool compactRuntime(JsonDocument& source, String& runtimeJson, String& keys) {
  DynamicJsonDocument compact(kRuntimeJsonBytes);
  compact["limits"] = source["limits"];
  JsonObject system = source["firmware_system"];
  if (system.isNull()) system = source["system"];
  if (!system.isNull()) compact["system"] = system;

  keys = "";
  JsonArray out = compact.createNestedArray("datastreams");
  size_t count = 0;
  for (JsonObject stream : source["datastreams"].as<JsonArray>()) {
    String key = stream["key"] | "";
    if (!key.length() || key.length() > 64 || ++count > FLOVA_DATASTREAM_CAPACITY) return false;
    if (keys.length() + key.length() + (keys.length() ? 1 : 0) > 768) return false;
    keys += (keys.length() ? "," : "") + key;

    JsonObject mapping = stream["hardware_mapping"];
    if (mapping.isNull()) continue;
    JsonObject row = out.createNestedObject();
    row["key"] = key;
    row["min_value"] = stream["min_value"];
    row["max_value"] = stream["max_value"];
    row["default_value"] = stream["default_value"];
    JsonObject mapped = row.createNestedObject("hardware_mapping");
    mapped["kind"] = mapping["kind"] | "";
    mapped["pin"] = mapping["pin"] | "";
    mapped["active_level"] = mapping["active_level"] | "high";
    mapped["pull"] = mapping["pull"] | "none";
    mapped["debounce_ms"] = mapping["debounce_ms"] | 50;
    mapped["sample_interval_ms"] = mapping["sample_interval_ms"] | 1000;
    mapped["min_output_interval_ms"] = mapping["min_output_interval_ms"] | 300;
  }
  if (!keys.length() || compact.overflowed() || measureJson(compact) >= kRuntimeJsonBytes) return false;
  runtimeJson = "";
  runtimeJson.reserve(measureJson(compact) + 1);
  return serializeJson(compact, runtimeJson) > 0;
}

inline bool configurationValid(const DeviceConfiguration& config) {
  return config.wifiSsid.length() > 0 && config.wifiSsid.length() <= 32 &&
         config.wifiPassword.length() <= 64 && config.deviceId.length() > 0 &&
         config.deviceId.length() <= 64 && config.mqttHost.length() > 0 &&
         config.mqttHost.length() <= 255 && config.mqttUsername.length() > 0 &&
         config.mqttUsername.length() <= 255 && config.mqttPassword.length() > 0 &&
         config.mqttPassword.length() <= 255 && config.datastreamKeys.length() > 0 &&
         config.datastreamKeys.length() <= 768 && config.runtimeJson.length() > 0 &&
         config.runtimeJson.length() < kRuntimeJsonBytes && config.mqttPort > 0;
}

inline bool encodeConfiguration(const DeviceConfiguration& config, String& json) {
  if (!configurationValid(config)) return false;
  DynamicJsonDocument document(kConfigurationJsonBytes);
  document["version"] = 1;
  document["wifi"]["ssid"] = config.wifiSsid;
  document["wifi"]["password"] = config.wifiPassword;
  document["device_id"] = config.deviceId;
  document["mqtt"]["host"] = config.mqttHost;
  document["mqtt"]["port"] = config.mqttPort;
  document["mqtt"]["username"] = config.mqttUsername;
  document["mqtt"]["password"] = config.mqttPassword;
  document["datastream_keys"] = config.datastreamKeys;
  document["runtime"] = config.runtimeJson;
  document["template_version_id"] = config.templateVersionId;
  document["checksum"] = config.checksum;
  if (document.overflowed() || measureJson(document) >= kConfigurationJsonBytes) return false;
  json = "";
  json.reserve(measureJson(document) + 1);
  return serializeJson(document, json) > 0;
}

inline bool decodeConfiguration(const String& json, DeviceConfiguration& config) {
  if (!json.length() || json.length() >= kConfigurationJsonBytes) return false;
  DynamicJsonDocument document(kConfigurationJsonBytes);
  if (deserializeJson(document, json) || document["version"] != 1) return false;
  config.wifiSsid = String((const char*)(document["wifi"]["ssid"] | ""));
  config.wifiPassword = String((const char*)(document["wifi"]["password"] | ""));
  config.deviceId = String((const char*)(document["device_id"] | ""));
  config.mqttHost = String((const char*)(document["mqtt"]["host"] | ""));
  config.mqttPort = document["mqtt"]["port"] | 0;
  config.mqttUsername = String((const char*)(document["mqtt"]["username"] | ""));
  config.mqttPassword = String((const char*)(document["mqtt"]["password"] | ""));
  config.datastreamKeys = String((const char*)(document["datastream_keys"] | ""));
  config.runtimeJson = String((const char*)(document["runtime"] | ""));
  config.templateVersionId = String((const char*)(document["template_version_id"] | ""));
  config.checksum = String((const char*)(document["checksum"] | ""));
  if (!configurationValid(config)) return false;
  DynamicJsonDocument runtime(kRuntimeJsonBytes);
  return !deserializeJson(runtime, config.runtimeJson) && runtime["datastreams"].is<JsonArray>();
}

}  // namespace flova
