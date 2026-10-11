#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include <FlovaEsp32.h>
#include <FlovaUniversalEsp32.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#include <FlovaEsp8266.h>
#include <FlovaUniversalEsp8266.h>
#elif defined(__linux__)
#include <FlovaLinux.h>
#else
#error "FlovaSDK supports Linux, ESP32, and ESP8266; use FlovaDevice.h for portable targets"
#endif
