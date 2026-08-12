#pragma once

#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <time.h>

#include <FlovaConfiguration.h>
#include <FlovaBoardProvisioning.h>

class FlovaEsp8266Storage : public flova::Storage {
 public:
  bool begin() { return LittleFS.begin(); }

  bool read(const char* key, void* output, size_t size) override {
    char path[48] = {};
    if (!makePath(key, path, sizeof(path)) || !output || !size) return false;
    File file = LittleFS.open(path, "r");
    if (!file) {
      char backup[48] = {};
      if (snprintf(backup, sizeof(backup), "%s.backup", path) >=
              static_cast<int>(sizeof(backup)) ||
          !LittleFS.exists(backup)) {
        return false;
      }
      file = LittleFS.open(backup, "r");
    }
    if (!file || static_cast<size_t>(file.size()) != size) {
      if (file) file.close();
      return false;
    }
    const size_t readBytes = file.read(reinterpret_cast<uint8_t*>(output), size);
    file.close();
    return readBytes == size;
  }

  bool write(const char* key, const void* value, size_t size) override {
    char path[48] = {}, next[48] = {};
    if (!makePath(key, path, sizeof(path)) ||
        snprintf(next, sizeof(next), "%s.next", path) >= static_cast<int>(sizeof(next)) ||
        !value || !size) return false;
    File file = LittleFS.open(next, "w");
    if (!file) return false;
    const bool written = file.write(reinterpret_cast<const uint8_t*>(value), size) == size;
    file.close();
    if (!written) {
      LittleFS.remove(next);
      return false;
    }
    char backup[48] = {};
    if (snprintf(backup, sizeof(backup), "%s.backup", path) >= static_cast<int>(sizeof(backup))) {
      LittleFS.remove(next);
      return false;
    }
    LittleFS.remove(backup);
    if (LittleFS.exists(path) && !LittleFS.rename(path, backup)) {
      LittleFS.remove(next);
      return false;
    }
    if (!LittleFS.rename(next, path)) {
      if (LittleFS.exists(backup)) LittleFS.rename(backup, path);
      return false;
    }
    LittleFS.remove(backup);
    return true;
  }

  bool remove(const char* key) override {
    char path[48] = {};
    if (!makePath(key, path, sizeof(path))) return false;
    return !LittleFS.exists(path) || LittleFS.remove(path);
  }

  flova::StorageCapabilities capabilities() const override {
    flova::StorageCapabilities value;
    value.usableBytes = 8192;
    value.availableBytes = 8192;
    value.maxRecordBytes = 1024;
    value.eraseBlockBytes = 4096;
    value.writeGranularity = 1;
    value.persistent = true;
    value.wearSensitive = true;
    return value;
  }

 private:
  static bool makePath(const char* key, char* output, size_t capacity) {
    return key && output && snprintf(output, capacity, "/%s.bin", key) < static_cast<int>(capacity);
  }
};

class FlovaEsp8266Provisioning : public FlovaBoardProvisioning {
 public:
  explicit FlovaEsp8266Provisioning(FlovaEsp8266Storage& storage) : storage_(storage) {}

  bool beginStorage() override { return storage_.begin(); }

  bool defaultHardwareId(char* output, size_t capacity) const override {
    return output && capacity >= 24 &&
           snprintf(output, capacity, "esp8266-%06lx",
                    static_cast<unsigned long>(ESP.getChipId())) > 0;
  }

  const char* defaultFirmwareTarget() const override {
    return "custom_arduino_esp8266";
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
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    char ssid[32] = {};
    snprintf(ssid, sizeof(ssid), "Flova-Setup-%06lx",
             static_cast<unsigned long>(ESP.getChipId()));
    if (!WiFi.softAP(ssid, nullptr, 1, false, 4)) return false;
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

  FlovaEsp8266Storage& storage_;
  ESP8266WebServer server_{80};
  FlovaProvisioningHandler handler_ = nullptr;
  void* context_ = nullptr;
  bool routesRegistered_ = false;
  bool provisioning_ = false;
  bool restartPending_ = false;
  uint32_t acceptedAt_ = 0;
};
