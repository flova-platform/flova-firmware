#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include <FlovaEsp32.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#include <FlovaEsp8266.h>
#else
#error "FlovaSDK supports ESP32 and ESP8266; use FlovaDevice.h for portable targets"
#endif
