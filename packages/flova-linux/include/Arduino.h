#pragma once

// Private compatibility surface for the shared bounded Device Link codec.
// Linux does not expose Arduino APIs to applications; this header only lets
// the already-tested codec use the same monotonic clock and log macros.
#include <stdint.h>
#include <stdio.h>
#include <time.h>

inline unsigned long millis() {
  struct timespec value = {};
  clock_gettime(CLOCK_MONOTONIC, &value);
  return static_cast<unsigned long>(value.tv_sec * 1000UL +
                                    value.tv_nsec / 1000000UL);
}

inline void yield() {}
#define F(value) value

class FlovaLinuxSerial {
 public:
  template <typename... Args>
  void printf(const char* format, Args... args) {
    ::fprintf(stderr, format, args...);
  }
  void println(const char* value) {
    if (value) ::fprintf(stderr, "%s\n", value);
  }
};

static FlovaLinuxSerial Serial;
