#pragma once
#include <FlovaLogger.h>

class ArduinoLogger : public FlovaLogger {
 public:
  void info(const char* message) override { Serial.println(message); }
  void error(const char* message) override { Serial.println(message); }
};
