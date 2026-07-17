#pragma once
#include <FlovaClock.h>
#include <time.h>

class ArduinoClock : public FlovaClock {
 public:
  uint32_t millisNow() override { return millis(); }
  bool utcValid() const override { return utcBaseMs_ != 0; }
  uint64_t utcMillis() const override { return utcValid() ? utcBaseMs_ + (uint32_t)(millis() - monotonicBaseMs_) : 0; }
  void setUtc(uint64_t value, uint32_t) override { utcBaseMs_ = value; monotonicBaseMs_ = millis(); }
  String isoNow() override {
    if (!utcValid()) return "";
    time_t seconds = (time_t)(utcMillis() / 1000); struct tm utc; gmtime_r(&seconds, &utc); char out[80];
    snprintf(out, sizeof(out), "%04d-%02d-%02dT%02d:%02d:%02dZ", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec); return String(out);
  }
 private:
  uint64_t utcBaseMs_ = 0;
  uint32_t monotonicBaseMs_ = 0;
};
