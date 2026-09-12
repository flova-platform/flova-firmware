#pragma once
// Classic ESP32 silicon mask, matching ESP-IDF's SOC_GPIO_VALID_GPIO_MASK.
#define GPIO_IS_VALID_GPIO(pin) ((pin) < 40 && ((0xFF0EFFFFFFULL >> (pin)) & 1))
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) < 34 && GPIO_IS_VALID_GPIO(pin))
