#pragma once

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <stdio.h>
#include <time.h>

#include <FlovaProvisioningAdapter.h>
#include <FlovaStorageKey.h>
#include <adapters/ArduinoFlovaUtcBootstrap.h>

class FlovaEsp32Storage : public flova::Storage {
 public:
  bool begin() override {
    if (ready_) return true;
    ready_ = preferences_.begin("flova", false);
    return ready_;
  }
  bool read(const char* key, void* output, size_t size) override {
    char physical[16] = {};
    return ready_ && output && size <= kMaximumRecordBytes &&
           flova::makeNvsStorageKey(key, physical, sizeof(physical)) &&
           preferences_.getBytesLength(physical) == size &&
           preferences_.getBytes(physical, output, size) == size;
  }
  bool write(const char* key, const void* value, size_t size) override {
    char physical[16] = {};
    return ready_ && value && size && size <= kMaximumRecordBytes &&
           flova::makeNvsStorageKey(key, physical, sizeof(physical)) &&
           preferences_.putBytes(physical, value, size) == size;
  }
  bool remove(const char* key) override {
    char physical[16] = {};
    return ready_ && flova::makeNvsStorageKey(key, physical, sizeof(physical)) &&
           (!preferences_.isKey(physical) || preferences_.remove(physical));
  }
  flova::StorageCapabilities capabilities() const override {
    flova::StorageCapabilities value;
    value.usableBytes = 16384;
    value.availableBytes = 16384;
    value.maxRecordBytes = kMaximumRecordBytes;
    value.eraseBlockBytes = 4096;
    value.writeGranularity = 1;
    value.persistent = true;
    value.wearSensitive = true;
    return value;
  }
 private:
  static const size_t kMaximumRecordBytes = 16384;
  Preferences preferences_;
  bool ready_ = false;
};

class FlovaEsp32Runtime : public FlovaProvisioningAdapter {
 public:
  void loop() override { utc_.run(runtimeConnected()); }
  bool runtimeConnected() const override { return WiFi.status() == WL_CONNECTED; }
  bool clockReady() const override { return utc_.ready(); }
  bool defaultHardwareId(char* output, size_t capacity) const override {
    if (!output || capacity < 25) return false;
    const uint64_t mac = ESP.getEfuseMac();
    const int written = snprintf(output, capacity, "esp32-%04lx%08lx",
        static_cast<unsigned long>((mac >> 32) & 0xffffUL),
        static_cast<unsigned long>(mac & 0xffffffffUL));
    return written > 0 && static_cast<size_t>(written) < capacity;
  }
  const char* defaultFirmwareTarget() const override {
    return "custom_arduino_esp32";
  }

 private:
  ArduinoFlovaUtcBootstrap<WiFiUDP> utc_;
};
