#pragma once

#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <time.h>

#include <FlovaConfiguration.h>
#include <FlovaProvisioningAdapter.h>
#include <FlovaWifiProvisioning.h>
#include <FlovaEsp8266Services.h>
#include <adapters/ArduinoFlovaUtcBootstrap.h>

class FlovaEsp8266Provisioning : public FlovaProvisioningAdapter {
 public:
  explicit FlovaEsp8266Provisioning(FlovaEsp8266Storage& storage) : storage_(storage) {}

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
    if (provisioning_) server_.handleClient();
    utc_.run(runtimeConnected());
  }

  bool startProvisioning() override {
    provisioning_ = false;
    if (!storage_.remove("wifi")) return false;
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
      Serial.printf("[flova] provisioning storage_failed stage=wifi reason=%s\n",
                    storage_.lastError());
      server_.send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
      return;
    }
    const FlovaProvisioningResponse result = handler_(context_, input_);
    if (result == FlovaProvisioningResponse::Accepted) {
      server_.send(202, "application/json", "{\"ok\":true,\"status\":\"accepted\"}");
    } else if (result == FlovaProvisioningResponse::StorageFailed) {
      Serial.println("[flova] provisioning storage_failed stage=handoff");
      server_.send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
    } else {
      storage_.remove("wifi");
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_handoff\"}");
    }
  }

  FlovaEsp8266Storage& storage_;
  ESP8266WebServer server_{80};
  FlovaProvisioningHandler handler_ = nullptr;
  void* context_ = nullptr;
  flova::ProvisioningHandoff input_;
  flova::WifiRuntimeData wifi_ = {};
  bool routesRegistered_ = false;
  bool provisioning_ = false;
  ArduinoFlovaUtcBootstrap<WiFiUDP> utc_;
};
