#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace flova {

inline bool copyStorageKey(const char* value, char* output, size_t capacity) {
  if (!value || !output || !capacity) return false;
  const size_t length = strlen(value);
  if (!length || length >= capacity) return false;
  memcpy(output, value, length + 1);
  return true;
}

inline bool parseStorageIndex(const char* value, unsigned maximum,
                              unsigned& output) {
  if (!value || !value[0]) return false;
  unsigned parsed = 0;
  for (const char* cursor = value; *cursor; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return false;
    const unsigned digit = static_cast<unsigned>(*cursor - '0');
    if (parsed > (maximum - digit) / 10U) return false;
    parsed = parsed * 10U + digit;
  }
  output = parsed;
  return true;
}

// ESP32 NVS keys are limited to 15 characters. Map the SDK's bounded logical
// key grammar to short, collision-free physical keys instead of hashing.
inline bool makeNvsStorageKey(const char* logical, char* output,
                              size_t capacity) {
  if (!logical || !output || capacity < 16) return false;
  struct FixedKey {
    const char* logical;
    const char* physical;
  };
  static const FixedKey fixed[] = {
      {"config", "cfg"},
      {"prov_pending", "pp"},
      {"prov_error", "pe"},
      {"wifi", "wf"},
      {"history.meta", "hm"},
      {"schedule.staging", "ss"},
      {"schedule.active", "sa"},
      {"schedule.progress", "sp"},
      {"session.pending", "xp"},
      {"session", "xs"},
      {"flova_l_a", "ca"},
      {"flova_l_b", "cb"},
      {"flova_l_p", "cp"},
      {"flova_l_0_m", "m0"},
      {"flova_l_1_m", "m1"},
  };
  for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
    if (strcmp(logical, fixed[i].logical) == 0)
      return copyStorageKey(fixed[i].physical, output, capacity);
  }

  const char* value = nullptr;
  const char* format = nullptr;
  unsigned maximum = 0;
  if (strncmp(logical, "history:", 8) == 0) {
    value = logical + 8;
    format = "h%u";
    maximum = 255;
  } else if (strncmp(logical, "dsid:", 5) == 0) {
    value = logical + 5;
    format = "d%u";
    maximum = 65535;
  } else if (strncmp(logical, "flova_l_0_r_", 12) == 0 ||
             strncmp(logical, "flova_l_1_r_", 12) == 0) {
    value = logical + 12;
    format = logical[8] == '0' ? "r0_%03u" : "r1_%03u";
    maximum = 999;
  } else {
    return false;
  }

  unsigned index = 0;
  if (!parseStorageIndex(value, maximum, index)) return false;
  const int written = snprintf(output, capacity, format, index);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

}  // namespace flova
