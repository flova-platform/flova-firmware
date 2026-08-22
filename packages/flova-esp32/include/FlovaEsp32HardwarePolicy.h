#pragma once

#include <stdint.h>

namespace flova {
namespace esp32 {

inline bool validInputPin(uint16_t pin) {
  return pin <= 39 && !(pin >= 6 && pin <= 11);
}

inline bool validOutputPin(uint16_t pin) {
  return pin <= 33 && !(pin >= 6 && pin <= 11);
}

inline bool validAnalogPin(uint16_t pin) { return pin >= 32 && pin <= 39; }

}  // namespace esp32
}  // namespace flova
