#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include <FlovaEsp32.h>
#else
#error "FlovaSDK installation through Arduino Library Manager supports ESP32"
#endif
