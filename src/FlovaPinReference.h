#pragma once
#include <stdint.h>
#include <string.h>

// Syntax only; physical meaning belongs to the board package.
inline bool flovaPinIndex(const char* reference, const char* prefix, uint16_t& pin) {
  const size_t length = strlen(prefix);
  if (strncmp(reference, prefix, length) != 0 || !reference[length]) return false;
  uint32_t value = 0;
  for (const char* digit = reference + length; *digit; ++digit) {
    if (*digit < '0' || *digit > '9') return false;
    value = value * 10 + (*digit - '0');
    if (value > UINT16_MAX) return false;
  }
  pin = static_cast<uint16_t>(value);
  return true;
}
