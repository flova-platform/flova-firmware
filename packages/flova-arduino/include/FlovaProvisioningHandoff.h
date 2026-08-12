#pragma once

#include <Arduino.h>
#include <FlovaConfiguration.h>
#include <FlovaWs.h>

namespace flova {

inline const char* findJsonLiteral(const char* begin, const char* end,
                                   const char* literal, size_t length) {
  if (!begin || !end || !literal || begin > end ||
      static_cast<size_t>(end - begin) < length) return 0;
  for (const char* cursor = begin; cursor + length <= end; ++cursor)
    if (memcmp(cursor, literal, length) == 0) return cursor;
  return 0;
}

inline bool jsonField(const char* json, const char* end, const char* name,
                      char* output, size_t capacity, bool required) {
  if (!json || !end || !name || !output || !capacity || json > end)
    return false;
  char needle[48] = {};
  const int needleLength = snprintf(needle, sizeof(needle), "\"%s\"", name);
  if (needleLength <= 0 || needleLength >= static_cast<int>(sizeof(needle)))
    return false;
  const char* cursor = findJsonLiteral(
      json, end, needle, static_cast<size_t>(needleLength));
  if (!cursor) return !required;
  if (findJsonLiteral(cursor + needleLength, end, needle,
                      static_cast<size_t>(needleLength))) return false;
  cursor += needleLength;
  while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                          *cursor == '\r' || *cursor == '\n')) ++cursor;
  if (cursor == end || *cursor++ != ':') return false;
  while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                          *cursor == '\r' || *cursor == '\n')) ++cursor;
  if (cursor == end || *cursor++ != '"') return false;
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
    if (static_cast<unsigned char>(value) < 0x20) return false;
    if (length + 1 >= capacity) return false;
    output[length++] = value;
  }
  if (cursor == end || *cursor != '"') return false;
  output[length] = 0;
  return required ? length != 0 : true;
}

inline bool parseProvisioningHandoff(const char* json, size_t length,
                                     ProvisioningHandoff& output) {
  if (!json || !length || length >= 768) return false;
  if (memchr(json, 0, length)) return false;
  const char* end = json + length;
  const char* first = json;
  while (first < end && (*first == ' ' || *first == '\t' ||
                         *first == '\r' || *first == '\n')) ++first;
  const char* last = end;
  while (last > first && (last[-1] == ' ' || last[-1] == '\t' ||
                          last[-1] == '\r' || last[-1] == '\n')) --last;
  if (last - first < 2 || *first != '{' || last[-1] != '}') return false;
  output = ProvisioningHandoff();
  if (!jsonField(first, last, "wifi_ssid", output.wifiSsid,
                 sizeof(output.wifiSsid), true) ||
      !jsonField(first, last, "wifi_password", output.wifiPassword,
                 sizeof(output.wifiPassword), false) ||
      !jsonField(first, last, "link_url", output.linkUrl,
                 sizeof(output.linkUrl), true) ||
      !jsonField(first, last, "token", output.token,
                 sizeof(output.token), true)) return false;
  return strncmp(output.linkUrl, "wss://", 6) == 0;
}

inline bool generateSecret(char (&output)[kSecretTextBytes],
                           FlovaEntropySource& entropy) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  uint8_t raw[32] = {};
  for (size_t i = 0; i < sizeof(raw); ++i) raw[i] = entropy.byte();
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
