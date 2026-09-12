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

#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define INPUT_PULLDOWN_16 4
#define HIGH 1
#define LOW 0
#define A0 17
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
int analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int value);
