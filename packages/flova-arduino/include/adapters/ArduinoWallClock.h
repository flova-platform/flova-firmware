#pragma once

#include <FlovaClock.h>
#include <FlovaScheduler.h>
#include <stdlib.h>
#include <time.h>

class ArduinoWallClock : public flova::WallClock {
 public:
  explicit ArduinoWallClock(const FlovaClock& clock) : clock_(clock) {}

  bool synchronized() const override { return clock_.utcValid(); }

  bool localTime(const char* timezone, flova::LocalTime& output) const override {
    if (!synchronized() || !timezone) return false;
    setenv("TZ", timezone, 1);
    tzset();
    time_t seconds = static_cast<time_t>(clock_.utcMillis() / 1000);
    struct tm local;
    if (!localtime_r(&seconds, &local)) return false;
    output.year = static_cast<uint16_t>(local.tm_year + 1900);
    output.month = static_cast<uint8_t>(local.tm_mon + 1);
    output.day = static_cast<uint8_t>(local.tm_mday);
    output.weekday = static_cast<uint8_t>(local.tm_wday);
    output.hour = static_cast<uint8_t>(local.tm_hour);
    output.minute = static_cast<uint8_t>(local.tm_min);
    return true;
  }

 private:
  const FlovaClock& clock_;
};
