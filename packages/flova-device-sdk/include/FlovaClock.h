#pragma once
#include <Arduino.h>

class FlovaClock {
 public:
  virtual uint32_t millisNow() = 0;
  virtual String isoNow() { return ""; }
  virtual bool utcValid() const { return false; }
  virtual uint64_t utcMillis() const { return 0; }
  virtual void setUtc(uint64_t, uint32_t) {}
};
