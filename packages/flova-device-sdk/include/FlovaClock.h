#pragma once
#include <Arduino.h>

class FlovaClock {
 public:
  virtual uint32_t millisNow() = 0;
  virtual String isoNow() { return ""; }
};
