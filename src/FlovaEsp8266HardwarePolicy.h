#pragma once

#include <Arduino.h>

namespace flova {
namespace esp8266 {

inline bool validDigitalPin(uint16_t pin) {
  return pin == 0 || pin == 2 || pin == 4 || pin == 5 ||
         (pin >= 12 && pin <= 16);
}

inline bool validAnalogPin(uint16_t pin) { return pin == A0; }

}  // namespace esp8266
}  // namespace flova
