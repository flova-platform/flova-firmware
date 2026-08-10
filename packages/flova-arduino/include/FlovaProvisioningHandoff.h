#pragma once

#include <Arduino.h>
#include <FlovaConfiguration.h>
#if defined(ESP32)
#include <esp_system.h>
#elif defined(ESP8266)
#include <user_interface.h>
#endif

namespace flova {

inline bool jsonField(const char* json, const char* name, char* output,
                      size_t capacity, bool required) {
  if (!json || !name || !output || !capacity) return false;
  char needle[48] = {};
  if (snprintf(needle, sizeof(needle), "\"%s\"", name) >= static_cast<int>(sizeof(needle))) return false;
  const char* cursor = strstr(json, needle);
  if (!cursor) return !required;
  cursor += strlen(needle);
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
  if (*cursor++ != ':') return false;
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
  if (*cursor++ != '"') return false;
  size_t length = 0;
  while (*cursor && *cursor != '"') {
    char value = *cursor++;
    if (value == '\\') {
      value = *cursor++;
      if (value == 'n') value = '\n';
      else if (value == 'r') value = '\r';
      else if (value == 't') value = '\t';
    }
    if (length + 1 >= capacity) return false;
    output[length++] = value;
  }
  if (*cursor != '"') return false;
  output[length] = 0;
  return required ? length != 0 : true;
}

inline bool parseProvisioningHandoff(const char* json, size_t length,
                                     ProvisioningHandoff& output) {
  if (!json || !length || length >= 768) return false;
  output = ProvisioningHandoff();
  if (!jsonField(json, "wifi_ssid", output.wifiSsid, sizeof(output.wifiSsid), true) ||
      !jsonField(json, "wifi_password", output.wifiPassword, sizeof(output.wifiPassword), false) ||
      !jsonField(json, "link_url", output.linkUrl, sizeof(output.linkUrl), true) ||
      !jsonField(json, "token", output.token, sizeof(output.token), true)) return false;
  return strncmp(output.linkUrl, "wss://", 6) == 0;
}

inline bool generateSecret(char (&output)[kSecretTextBytes]) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  uint8_t raw[32] = {};
#if defined(ESP32)
  for (size_t i = 0; i < sizeof(raw); i += 4) {
    const uint32_t value = esp_random();
    memcpy(raw + i, &value, (sizeof(raw) - i >= 4) ? 4 : sizeof(raw) - i);
  }
#elif defined(ESP8266)
  for (size_t i = 0; i < sizeof(raw); ++i) raw[i] = static_cast<uint8_t>(os_random());
#else
  for (size_t i = 0; i < sizeof(raw); ++i) raw[i] = static_cast<uint8_t>(random(0, 256));
#endif
  size_t outputLength = 0;
  uint32_t bits = 0;
  uint8_t available = 0;
  for (size_t i = 0; i < sizeof(raw); ++i) {
    bits = (bits << 8) | raw[i];
    available = static_cast<uint8_t>(available + 8);
    while (available >= 6) {
      available = static_cast<uint8_t>(available - 6);
      output[outputLength++] = alphabet[(bits >> available) & 63];
    }
  }
  if (available) output[outputLength++] = alphabet[(bits << (6 - available)) & 63];
  output[outputLength] = 0;
  return outputLength == 43;
}

}  // namespace flova
