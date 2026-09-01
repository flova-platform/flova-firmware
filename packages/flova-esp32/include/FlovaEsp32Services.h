#pragma once

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <stdio.h>
#include <time.h>

#include <FlovaRuntimeServices.h>
#include <FlovaEsp32StorageKey.h>
#include <FlovaWifiProvisioning.h>
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
           preferences_.isKey(physical) &&
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
  bool clear() override { return ready_ && preferences_.clear(); }
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

class FlovaEsp32ObservedNetwork final : public FlovaNetworkRuntime {
 public:
  bool connected() const override { return WiFi.status() == WL_CONNECTED; }
};

class FlovaEsp32StoredNetwork final : public FlovaNetworkRuntime {
 public:
  explicit FlovaEsp32StoredNetwork(FlovaEsp32Storage& storage)
      : storage_(storage) {}

  bool begin() override {
    flova::WifiRuntimeData wifi = {};
    if (!storage_.read("wifi", &wifi, sizeof(wifi)) ||
        !flova::validWifiRuntimeData(wifi))
      return false;
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifi.ssid, wifi.password);
    return true;
  }

  bool stop() override {
    if (WiFi.getMode() & WIFI_MODE_STA) WiFi.disconnect(false, false);
    return true;
  }
  bool clearCredentials() override {
    WiFi.disconnect(true, true);
    return storage_.remove("wifi");
  }

  bool connected() const override { return WiFi.status() == WL_CONNECTED; }

 private:
  FlovaEsp32Storage& storage_;
};

class FlovaEsp32PlatformNetwork final : public FlovaNetworkRuntime {
 public:
  bool begin() override {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    const wl_status_t status = WiFi.begin();
    return status != WL_CONNECT_FAILED && status != WL_NO_SHIELD;
  }

  bool stop() override {
    if (WiFi.getMode() & WIFI_MODE_STA) WiFi.disconnect(false, false);
    return true;
  }

  bool connected() const override { return WiFi.status() == WL_CONNECTED; }
};

class FlovaEsp32Identity final : public FlovaBoardIdentity {
 public:
  explicit FlovaEsp32Identity(const char* firmwareTarget)
      : firmwareTarget_(firmwareTarget) {}

  bool hardwareId(char* output, size_t capacity) const override {
    if (!output || capacity < 25) return false;
    const uint64_t mac = ESP.getEfuseMac();
    const int written = snprintf(output, capacity, "esp32-%04lx%08lx",
        static_cast<unsigned long>((mac >> 32) & 0xffffUL),
        static_cast<unsigned long>(mac & 0xffffffffUL));
    return written > 0 && static_cast<size_t>(written) < capacity;
  }

  const char* firmwareTarget() const override { return firmwareTarget_; }

 private:
  const char* firmwareTarget_;
};
