#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <FlovaConfiguration.h>

namespace flova {

struct WifiRuntimeData {
  char ssid[33];
  char password[65];
};

static const size_t kWifiRuntimeDataBytes = sizeof(WifiRuntimeData);

inline void skipJsonWhitespace(const char*& cursor, const char* end) {
  while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                          *cursor == '\r' || *cursor == '\n'))
    ++cursor;
}

inline bool readJsonString(const char*& cursor, const char* end, char* output,
                           size_t capacity) {
  if (!output || !capacity || cursor == end || *cursor++ != '"') return false;
  size_t length = 0;
  while (cursor < end && *cursor != '"') {
    char value = *cursor++;
    if (value == '\\') {
      if (cursor == end) return false;
      value = *cursor++;
      if (value == 'n') value = '\n';
      else if (value == 'r') value = '\r';
      else if (value == 't') value = '\t';
      else if (value != '"' && value != '\\' && value != '/') return false;
    }
    if (static_cast<unsigned char>(value) < 0x20 || length + 1 >= capacity)
      return false;
    output[length++] = value;
  }
  if (cursor == end || *cursor++ != '"') return false;
  output[length] = 0;
  return true;
}

inline bool makeWifiRuntimeData(const char* ssid, const char* password,
                                WifiRuntimeData& data) {
  if (!ssid || !ssid[0]) return false;
  data = WifiRuntimeData();
  const size_t ssidLength = strlen(ssid);
  const size_t passwordLength = password ? strlen(password) : 0;
  if (ssidLength >= sizeof(data.ssid) || passwordLength >= sizeof(data.password))
    return false;
  memcpy(data.ssid, ssid, ssidLength + 1);
  if (passwordLength) memcpy(data.password, password, passwordLength + 1);
  return true;
}

inline bool validWifiRuntimeData(const WifiRuntimeData& output) {
  return output.ssid[0] && strnlen(output.ssid, sizeof(output.ssid)) <
                                sizeof(output.ssid) &&
         strnlen(output.password, sizeof(output.password)) <
             sizeof(output.password);
}

inline bool parseWifiProvisioningHandoff(const char* json, size_t length,
                                         ProvisioningHandoff& output,
                                         WifiRuntimeData* wifiOutput = nullptr) {
  if (!json || !length || length >= 768 || memchr(json, 0, length))
    return false;
  const char* end = json + length;
  const char* first = json;
  while (first < end && (*first == ' ' || *first == '\t' ||
                         *first == '\r' || *first == '\n'))
    ++first;
  const char* last = end;
  while (last > first && (last[-1] == ' ' || last[-1] == '\t' ||
                          last[-1] == '\r' || last[-1] == '\n'))
    --last;
  if (last - first < 2 || *first != '{' || last[-1] != '}') return false;

  output = ProvisioningHandoff();
  WifiRuntimeData wifi = {};
  uint8_t seen = 0;
  const char* cursor = first + 1;
  for (;;) {
    skipJsonWhitespace(cursor, last - 1);
    if (cursor == last - 1) break;
    char name[24] = {};
    if (!readJsonString(cursor, last - 1, name, sizeof(name))) return false;
    skipJsonWhitespace(cursor, last - 1);
    if (cursor == last - 1 || *cursor++ != ':') return false;
    skipJsonWhitespace(cursor, last - 1);
    uint8_t field = 0;
    char* destination = nullptr;
    size_t capacity = 0;
    if (strcmp(name, "wifi_ssid") == 0) {
      field = 1; destination = wifi.ssid; capacity = sizeof(wifi.ssid);
    } else if (strcmp(name, "wifi_password") == 0) {
      field = 2; destination = wifi.password; capacity = sizeof(wifi.password);
    } else if (strcmp(name, "link_url") == 0) {
      field = 4; destination = output.linkUrl; capacity = sizeof(output.linkUrl);
    } else if (strcmp(name, "token") == 0) {
      field = 8; destination = output.token; capacity = sizeof(output.token);
    } else {
      return false;
    }
    if ((seen & field) ||
        !readJsonString(cursor, last - 1, destination, capacity))
      return false;
    seen |= field;
    skipJsonWhitespace(cursor, last - 1);
    if (cursor == last - 1) break;
    if (*cursor++ != ',') return false;
    skipJsonWhitespace(cursor, last - 1);
    if (cursor == last - 1) return false;
  }
  const uint8_t required = wifiOutput ? 13 : 12;
  if ((seen & required) != required || !output.linkUrl[0] ||
      !output.token[0] || (wifiOutput && !wifi.ssid[0])) return false;
  if (strncmp(output.linkUrl, "wss://", 6) != 0) return false;
  if (wifiOutput) *wifiOutput = wifi;
  return true;
}

}  // namespace flova
