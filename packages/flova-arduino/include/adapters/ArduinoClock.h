#pragma once
#include <FlovaClock.h>

class ArduinoClock : public FlovaClock {
 public:
  uint32_t millisNow() override { return millis(); }
};
