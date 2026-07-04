#pragma once
#include <Arduino.h>

struct FlovaConfig {
  const char* deviceId = "";
  const char* mqttHost = "";
  uint16_t mqttPort = 1883;
  const char* mqttUsername = "";
  const char* mqttPassword = "";
  const char* firmwareVersion = "0.1.0";
  const char* sdkVersion = "0.1.0";
  const char* protocolVersion = "datastream-v1";
  const char* boardType = "esp32";
  bool otaCapable = false;
  bool rollbackCapable = false;
  uint32_t heartbeatIntervalMs = 30000;
  uint32_t flashSize = 0;
  const char* datastreamKeys = "";
};

class FlovaValue {
 public:
  explicit FlovaValue(String raw) : raw_(raw) {}
  bool asBool() const { return raw_ == "true" || raw_ == "1"; }
  double asDouble() const { return raw_.toDouble(); }
  String asString() const { return raw_; }

 private:
  String raw_;
};

typedef bool (*FlovaWriteHandler)(FlovaValue value);
