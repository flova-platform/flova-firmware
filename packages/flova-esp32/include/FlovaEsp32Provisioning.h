#pragma once

#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>

#include <FlovaConfiguration.h>
#include <FlovaProvisioningAdapter.h>
#include <FlovaWifiProvisioning.h>
#include <FlovaEsp32Services.h>
#include <adapters/ArduinoFlovaUtcBootstrap.h>

class FlovaEsp32Provisioning : public FlovaProvisioningAdapter {
 public:
  explicit FlovaEsp32Provisioning(FlovaEsp32Storage& storage) : storage_(storage) {}

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
    if (provisioning_) server_.handleClient();
    utc_.run(runtimeConnected());
  }

  bool startProvisioning() override {
    provisioning_ = false;
    if (!storage_.remove("wifi")) return false;
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

  bool beginRuntime() override {
    flova::WifiRuntimeData wifi = {};
    if (!storage_.read("wifi", &wifi, sizeof(wifi)) ||
        !flova::validWifiRuntimeData(wifi)) return false;
    provisioning_ = false;
    server_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifi.ssid, wifi.password);
    return true;
  }

  bool runtimeConnected() const override { return WiFi.status() == WL_CONNECTED; }

  bool clockReady() const override { return utc_.ready(); }

 private:
  void handleStatus() {
    char error[flova::kProvisioningErrorBytes] = {};
    char* body = reinterpret_cast<char*>(&input_);
    const size_t bodyCapacity = sizeof(input_);
    const bool hasError =
        storage_.read("prov_error", error, sizeof(error)) && error[0];
    snprintf(body, bodyCapacity,
             hasError
                 ? "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"can_retry\":true,\"last_error_code\":\"%s\"}"
                 : "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"can_retry\":true}",
             error);
    server_.send(200, "application/json", body);
  }

  void handleProvision() {
    const String& body = server_.arg("plain");
    if (!handler_ || body.length() >= 768 ||
        !flova::parseWifiProvisioningHandoff(body.c_str(), body.length(), input_,
                                             &wifi_)) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_handoff\"}");
      return;
    }
    if (!storage_.write("wifi", &wifi_, sizeof(wifi_))) {
      server_.send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
      return;
    }
    const FlovaProvisioningResponse result = handler_(context_, input_);
    if (result == FlovaProvisioningResponse::Accepted) {
      server_.send(202, "application/json", "{\"ok\":true,\"status\":\"accepted\"}");
    } else if (result == FlovaProvisioningResponse::StorageFailed) {
      server_.send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
    } else {
      storage_.remove("wifi");
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_handoff\"}");
    }
  }

  FlovaEsp32Storage& storage_;
  WebServer server_{80};
  FlovaProvisioningHandler handler_ = nullptr;
  void* context_ = nullptr;
  flova::ProvisioningHandoff input_;
  flova::WifiRuntimeData wifi_ = {};
  bool routesRegistered_ = false;
  bool provisioning_ = false;
  ArduinoFlovaUtcBootstrap<WiFiUDP> utc_;
};
