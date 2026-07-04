#pragma once
#include <Arduino.h>

typedef void (*FlovaMessageCallback)(const String& topic, const String& payload);

class FlovaTransport {
 public:
  virtual bool begin() = 0;
  virtual bool connected() = 0;
  virtual bool connect(const char* clientId, const char* username, const char* password) = 0;
  virtual bool publish(const char* topic, const String& payload) = 0;
  virtual bool subscribe(const char* topic) = 0;
  virtual void setCallback(FlovaMessageCallback callback) = 0;
  virtual void loop() = 0;
};
