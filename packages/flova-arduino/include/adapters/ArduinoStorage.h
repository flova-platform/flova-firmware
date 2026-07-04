#pragma once
#include <FlovaStorage.h>

class ArduinoStorage : public FlovaStorage {
 public:
  bool getString(const char*, char*, size_t) override { return false; }
  bool setString(const char*, const char*) override { return true; }
  bool remove(const char*) override { return true; }
  void clear() override {}
};
