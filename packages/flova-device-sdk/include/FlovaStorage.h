#pragma once
#include <Arduino.h>

class FlovaStorage {
 public:
  virtual bool getString(const char* key, char* out, size_t maxLen) = 0;
  virtual bool setString(const char* key, const char* value) = 0;
  virtual bool remove(const char* key) = 0;
  virtual void clear() = 0;
};
