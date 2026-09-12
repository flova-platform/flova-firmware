#pragma once

#include <Arduino.h>
#include <driver/gpio.h>
#include <FlovaPinReference.h>

namespace flova {
namespace esp32 {

inline bool validInputPin(uint16_t pin) {
  return pin < 40 && pin != 20 && GPIO_IS_VALID_GPIO(pin) && !(pin >= 6 && pin <= 11);
}

inline bool validOutputPin(uint16_t pin) {
  return validInputPin(pin) && GPIO_IS_VALID_OUTPUT_GPIO(pin);
}

inline bool resolvePin(const char* reference, uint16_t& pin) {
  uint16_t channel;
  static const uint8_t adc1[] = {36, 37, 38, 39, 32, 33, 34, 35};
  static const uint8_t adc2[] = {4, 0, 2, 15, 13, 12, 14, 27, 25, 26};
  if (flovaPinIndex(reference, "ADC1_CH", channel) && channel < sizeof(adc1)) {
    pin = adc1[channel]; return true;
  }
  if (flovaPinIndex(reference, "ADC2_CH", channel) && channel < sizeof(adc2)) {
    pin = adc2[channel]; return true;
  }
  return flovaPinIndex(reference, "GPIO", pin);
}

inline bool inputMode(uint16_t pin, uint8_t pull, uint8_t& mode) {
  if (!validInputPin(pin) || pull > 2 || (pin >= 34 && pull != 0)) return false;
  mode = pull == 1 ? INPUT_PULLUP : pull == 2 ? INPUT_PULLDOWN : INPUT;
  return true;
}

inline bool validAnalogPin(uint16_t pin) { return validInputPin(pin) && pin >= 32 && pin <= 39; }

}  // namespace esp32
}  // namespace flova
