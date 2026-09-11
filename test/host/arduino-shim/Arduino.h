#pragma once

#include <stddef.h>
#include <stdint.h>

class HardwareSerial {
 public:
  void println(const char*) {}
  int printf(const char*, ...) { return 0; }
};

extern HardwareSerial Serial;

uint32_t millis();
unsigned long micros();
void delay(unsigned long milliseconds);
