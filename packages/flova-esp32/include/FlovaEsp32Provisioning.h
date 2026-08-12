#pragma once

#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include <FlovaConfiguration.h>
#include <FlovaBoardProvisioning.h>

class FlovaEsp32Storage : public flova::Storage {
 public:
  bool begin() { return preferences_.begin("flova", false); }

  bool read(const char* key, void* output, size_t size) override {
    return key && output && size && preferences_.getBytesLength(key) == size &&
           preferences_.getBytes(key, output, size) == size;
  }

  bool write(const char* key, const void* value, size_t size) override {
    return key && value && size && preferences_.putBytes(key, value, size) == size;
  }

  bool remove(const char* key) override {
    return key && (!preferences_.isKey(key) || preferences_.remove(key));
  }

  flova::StorageCapabilities capabilities() const override {
    flova::StorageCapabilities value;
    value.usableBytes = 4096;
    value.availableBytes = 4096;
    value.maxRecordBytes = 1024;
    value.eraseBlockBytes = 4096;
    value.writeGranularity = 1;
    value.persistent = true;
    value.wearSensitive = true;
    return value;
  }

 private:
  Preferences preferences_;
};

class FlovaEsp32Provisioning : public FlovaBoardProvisioning {
 public:
  explicit FlovaEsp32Provisioning(FlovaEsp32Storage& storage) : storage_(storage) {}

  bool beginStorage() override { return storage_.begin(); }

  bool defaultHardwareId(char* output, size_t capacity) const override {
    if (!output || capacity < 25) return false;
    const uint64_t mac = ESP.getEfuseMac();
    const int written = snprintf(
        output, capacity, "esp32-%04lx%08lx",
        static_cast<unsigned long>((mac >> 32) & 0xffffUL),
        static_cast<unsigned long>(mac & 0xffffffffUL));
    return written > 0 && static_cast<size_t>(written) < capacity;
  }

  const char* defaultFirmwareTarget() const override {
    return "custom_arduino_esp32";
  }

  bool begin(FlovaProvisioningHandler handler, void* context) override {
    handler_ = handler;
    context_ = context;
    if (!routesRegistered_) {
      server_.on("/status", HTTP_GET, [this]() { handleStatus(); });
      server_.on("/provision", HTTP_POST, [this]() { handleProvision(); });
      routesRegistered_ = true;
    }
    return true;
  }

  void loop() override {
    if (provisioning_) {
      server_.handleClient();
    }
    if (restartPending_ && millis() - acceptedAt_ >= 1000UL) {
      restartPending_ = false;
      server_.stop();
      WiFi.softAPdisconnect(true);
      ESP.restart();
    }
  }

  bool startProvisioning() override {
    provisioning_ = false;
    restartPending_ = false;
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
    char ssid[32] = {};
    snprintf(ssid, sizeof(ssid), "Flova-Setup-%08lx",
             static_cast<unsigned long>(ESP.getEfuseMac()));
    if (!WiFi.softAP(ssid)) return false;
    server_.begin();
    provisioning_ = true;
    return true;
  }

  bool provisioning() const override { return provisioning_; }

  bool beginStation(const char* ssid, const char* password) override {
    if (!ssid || !ssid[0]) return false;
    provisioning_ = false;
    server_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password ? password : "");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    return true;
  }

  bool stationConnected() const override { return WiFi.status() == WL_CONNECTED; }

  bool clockReady() const override { return time(nullptr) >= 1700000000; }

  void scheduleRestart() override {
    restartPending_ = true;
    acceptedAt_ = millis();
  }

 private:
  void handleStatus() {
    char error[flova::kProvisioningErrorBytes] = {};
    String body = "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"can_retry\":true";
    if (storage_.read("prov_error", error, sizeof(error)) && error[0]) {
      body += ",\"last_error_code\":\"";
      body += error;
      body += "\"";
    }
    body += "}";
    server_.send(200, "application/json", body);
  }

  void handleProvision() {
    String body = server_.arg("plain");
    if (!handler_ || body.length() >= 768) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_handoff\"}");
      return;
    }
    const FlovaProvisioningResponse result =
        handler_(context_, body.c_str(), body.length());
    if (result == FlovaProvisioningResponse::Accepted) {
      server_.send(202, "application/json", "{\"ok\":true,\"status\":\"accepted\"}");
      scheduleRestart();
    } else if (result == FlovaProvisioningResponse::StorageFailed) {
      server_.send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
    } else {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_handoff\"}");
    }
  }

  FlovaEsp32Storage& storage_;
  WebServer server_{80};
  FlovaProvisioningHandler handler_ = nullptr;
  void* context_ = nullptr;
  bool routesRegistered_ = false;
  bool provisioning_ = false;
  bool restartPending_ = false;
  uint32_t acceptedAt_ = 0;
};
