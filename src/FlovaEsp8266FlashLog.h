#pragma once

#if defined(ESP8266)
#include <FlovaLogging.h>
#include <pgmspace.h>

namespace flova {
// Isolate the bounded flash-to-Logger copy from the caller's stack frame.
template <typename Logger>
__attribute__((noinline)) void logFlashLiteral(Logger& logger, PGM_P text) {
  char line[128];
  strncpy_P(line, text, sizeof(line) - 1);
  line[sizeof(line) - 1] = 0;
  logger.log(line);
}
}
#ifndef FLOVA_LOG_RAW
#define FLOVA_LOG_RAW(logger, literal) ::flova::logFlashLiteral(logger, PSTR(literal))
#endif
#define FLOVA_FORMAT(out, size, literal, ...) snprintf_P(out, size, PSTR(literal), __VA_ARGS__)
#define FLOVA_SERIAL_PRINTF_RAW(literal, ...) Serial.printf_P(PSTR(literal), __VA_ARGS__)
#define FLOVA_SERIAL_PRINTLN_RAW(literal) Serial.println(F(literal))
#endif
