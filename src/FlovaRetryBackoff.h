#pragma once
#include <stdint.h>

namespace flova {
class RetryBackoff {
 public:
  uint32_t next(uint8_t jitter) {
    const uint32_t result = delay_ - delay_ / 8 +
        (delay_ / 8 * static_cast<uint32_t>(jitter)) / 255;
    delay_ = delay_ >= 30000 ? 60000 : delay_ * 2;
    return result;
  }
  void reset() { delay_ = 1000; }
 private:
  uint32_t delay_ = 1000;
};
}
