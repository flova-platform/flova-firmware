#pragma once
#include <Arduino.h>

class FlovaStorage {
 public:
  // Binary records are used only for bounded transactional protocol/storage
  // metadata. They are never a public wire format or a JSON replacement.
  virtual bool read(const char* key, void* output, size_t size) const {
    (void)key;
    (void)output;
    (void)size;
    return false;
  }
  virtual bool write(const char* key, const void* value, size_t size) {
    (void)key;
    (void)value;
    (void)size;
    return false;
  }
  virtual bool getString(const char* key, String& out) { (void)key; (void)out; return false; }
  virtual bool getString(const char* key, char* out, size_t maxLen) = 0;
  virtual bool setString(const char* key, const char* value) = 0;
  virtual bool remove(const char* key) = 0;
  virtual void clear() = 0;
};
