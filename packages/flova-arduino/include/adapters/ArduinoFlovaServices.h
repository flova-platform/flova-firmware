#pragma once

#include <Arduino.h>
#include <time.h>

#include <FlovaDevice.h>

class ArduinoFlovaClock : public flova::Clock {
 public:
  uint64_t milliseconds() const override { return millis(); }
  bool utcValid() const override { return utcBaseMs_ != 0; }
  uint64_t utcMilliseconds() const override {
    return utcValid() ? utcBaseMs_ + (millis() - monotonicBaseMs_) : 0;
  }
  void setUtc(uint64_t value, uint64_t) override {
    utcBaseMs_ = value;
    monotonicBaseMs_ = millis();
  }

 private:
  uint64_t utcBaseMs_ = 0;
  uint32_t monotonicBaseMs_ = 0;
};

// The default client is intentionally volatile. Applications that need local
// persistence inject their own flova::Storage implementation instead of
// making the SDK guess which filesystem or wear policy their board uses.
class ArduinoFlovaStorage : public flova::Storage {
 public:
  bool read(const char*, void*, size_t) override { return false; }
  bool write(const char*, const void*, size_t) override { return false; }
  bool remove(const char*) override { return true; }
};

class ArduinoFlovaLogger : public flova::Logger {
 public:
  void log(const char* message) override {
    if (message) Serial.println(message);
  }
};
