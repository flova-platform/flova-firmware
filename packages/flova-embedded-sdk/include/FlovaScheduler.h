#pragma once

#include <stddef.h>
#include <stdint.h>

namespace flova {

struct LocalTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t weekday;  // 0 = Sunday
  uint8_t hour;
  uint8_t minute;
};

// Boards own UTC synchronization and timezone conversion. ESP targets can use
// POSIX TZ/localtime_r; custom boards may use an RTC or a host clock service.
class WallClock {
 public:
  virtual ~WallClock() {}
  virtual bool synchronized() const = 0;
  virtual bool localTime(const char* posixTimezone, LocalTime& output) const = 0;
};

typedef void (*ScheduleHandler)();

class Scheduler {
 public:
  static const size_t kCapacity = 8;
  explicit Scheduler(WallClock& clock) : clock_(clock), count_(0) {}

  bool daily(uint8_t hour, uint8_t minute, const char* timezone,
             ScheduleHandler handler, uint8_t weekdays = 0x7f) {
    if (count_ == kCapacity || hour > 23 || minute > 59 || !timezone ||
        !handler || weekdays == 0) return false;
    Entry& entry = entries_[count_++];
    entry.hour = hour;
    entry.minute = minute;
    entry.weekdays = weekdays;
    entry.timezone = timezone;
    entry.handler = handler;
    entry.lastDate = 0;
    return true;
  }

  void run() {
    if (!clock_.synchronized()) return;
    for (size_t i = 0; i < count_; ++i) {
      LocalTime now;
      Entry& entry = entries_[i];
      if (!clock_.localTime(entry.timezone, now) || now.weekday > 6 ||
          !(entry.weekdays & (1u << now.weekday)) || now.hour != entry.hour ||
          now.minute != entry.minute) continue;
      const uint32_t date = static_cast<uint32_t>(now.year) * 10000UL +
                            static_cast<uint32_t>(now.month) * 100UL + now.day;
      if (entry.lastDate == date) continue;  // run once during the due minute
      entry.lastDate = date;
      entry.handler();
    }
  }

 private:
  struct Entry {
    uint8_t hour, minute, weekdays;
    const char* timezone;
    ScheduleHandler handler;
    uint32_t lastDate;
  };
  WallClock& clock_;
  Entry entries_[kCapacity];
  size_t count_;
};

}  // namespace flova
