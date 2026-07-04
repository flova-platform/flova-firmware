#pragma once
#include <Arduino.h>

class FlovaLogger {
 public:
  virtual void info(const char* message) = 0;
  virtual void error(const char* message) = 0;
};
