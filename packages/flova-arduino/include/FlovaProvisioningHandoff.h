#pragma once

#include <stdint.h>

#include <FlovaConfiguration.h>
#include <FlovaWs.h>

namespace flova {

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
  if (available)
    output[outputLength++] = alphabet[(bits << (6 - available)) & 63];
  output[outputLength] = 0;
  return outputLength == 43;
}

}  // namespace flova
