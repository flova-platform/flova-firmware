#pragma once

#include <Arduino.h>
#include <FlovaPinReference.h>

namespace flova {
namespace esp8266 {

inline bool validDigitalPin(uint16_t pin) {
  return pin == 0 || pin == 2 || pin == 4 || pin == 5 ||
         (pin >= 12 && pin <= 16);
}

inline bool resolvePin(const char* reference, uint16_t& pin) {
  if (!strcmp(reference, "A0") || !strcmp(reference, "ADC0") || !strcmp(reference, "TOUT")) {
    pin = A0;
    return true;
  }
  return flovaPinIndex(reference, "GPIO", pin);
}

inline bool inputMode(uint16_t pin, uint8_t pull, uint8_t& mode) {
  if (!validDigitalPin(pin)) return false;
  if (pull == 0) { mode = INPUT; return true; }
  if (pull == 1 && pin != 16) { mode = INPUT_PULLUP; return true; }
  if (pull == 2 && pin == 16) { mode = INPUT_PULLDOWN_16; return true; }
  return false;
}

inline bool validAnalogPin(uint16_t pin) { return pin == A0; }

}  // namespace esp8266
}  // namespace flova
