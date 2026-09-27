#pragma once

#include <new>

#include <WebServer.h>
#include <WiFi.h>
#include <FlovaConfiguration.h>
#include <FlovaProvisioningAdapter.h>
#include <FlovaSoftApPortal.h>
#include <FlovaWifiProvisioning.h>
#include <FlovaEsp32Services.h>

class FlovaEsp32Provisioning : public FlovaProvisioningAdapter {
 public:
  explicit FlovaEsp32Provisioning(FlovaEsp32Storage& storage,
                                  const char* setupPassword = nullptr)
      : storage_(storage), setupPassword_(setupPassword) {}

  ~FlovaEsp32Provisioning() override { delete setup_; }
  FlovaEsp32Provisioning(const FlovaEsp32Provisioning&) = delete;
  FlovaEsp32Provisioning& operator=(const FlovaEsp32Provisioning&) = delete;

  bool begin(FlovaProvisioningHandler handler, void* context) override {
    handler_ = handler;
    context_ = context;
    return true;
  }

  void loop() override {
    if (provisioning_) setup_->server.handleClient();
  }

  bool startProvisioning() override {
    stopProvisioning();
    if (!storage_.remove("wifi")) return false;
    setup_ = new (std::nothrow) Setup;
    if (!setup_) return false;
    setup_->server.on("/setup", HTTP_GET, [this]() { handleSetup(); });
    setup_->server.on("/status", HTTP_GET, [this]() { handleStatus(); });
    setup_->server.on("/provision", HTTP_POST, [this]() { handleProvision(); });
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
    char ssid[32] = {};
    snprintf(ssid, sizeof(ssid), "Flova-Setup-%08lx",
             static_cast<unsigned long>(ESP.getEfuseMac()));
    if (setupPassword_) {
      const size_t length = strlen(setupPassword_);
      if (length < 8 || length > 63) { stopProvisioning(); return false; }
    }
    if (!WiFi.softAP(ssid, setupPassword_)) { stopProvisioning(); return false; }
    setup_->server.begin();
    provisioning_ = true;
    return true;
  }

  bool stopProvisioning() override {
    provisioning_ = false;
    if (setup_) { setup_->server.stop(); delete setup_; setup_ = nullptr; }
    WiFi.softAPdisconnect(true);
    return true;
  }

  bool stopAfterNetworkConnected() const override { return true; }

 private:
  void handleSetup() {
    setup_->server.sendHeader("Cache-Control", "no-store");
    setup_->server.sendHeader("X-Frame-Options", "DENY");
    setup_->server.send_P(200, "text/html; charset=utf-8", flova::kSoftApSetupPage);
  }

  void handleStatus() {
    char error[flova::kProvisioningErrorBytes] = {};
    char* body = reinterpret_cast<char*>(&setup_->input);
    const size_t bodyCapacity = sizeof(setup_->input);
    const bool hasError =
        storage_.read("prov_error", error, sizeof(error)) && error[0];
    snprintf(body, bodyCapacity,
             hasError
                 ? "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"browser_handoff\":\"fragment-v1\",\"can_retry\":true,\"last_error_code\":\"%s\"}"
                 : "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"browser_handoff\":\"fragment-v1\",\"can_retry\":true}",
             error);
    setup_->server.send(200, "application/json", body);
  }

  void handleProvision() {
    const String& body = setup_->server.arg("plain");
    if (!handler_ || body.length() >= 768 ||
        !flova::parseWifiProvisioningHandoff(body.c_str(), body.length(), setup_->input,
                                             &setup_->wifi)) {
      setup_->server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_handoff\"}");
      return;
    }
    if (!storage_.write("wifi", &setup_->wifi, sizeof(setup_->wifi))) {
      setup_->server.send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
      return;
    }
    const FlovaProvisioningResponse result = handler_(context_, setup_->input);
    if (result == FlovaProvisioningResponse::Accepted) {
      setup_->server.send(202, "application/json", "{\"ok\":true,\"status\":\"accepted\"}");
    } else if (result == FlovaProvisioningResponse::StorageFailed) {
      setup_->server.send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
    } else {
      storage_.remove("wifi");
      setup_->server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_handoff\"}");
    }
  }

  FlovaEsp32Storage& storage_;
  const char* setupPassword_;
  struct Setup {
    WebServer server{80};
    flova::ProvisioningHandoff input;
    flova::WifiRuntimeData wifi = {};
  };
  Setup* setup_ = nullptr;
  FlovaProvisioningHandler handler_ = nullptr;
  void* context_ = nullptr;
  bool provisioning_ = false;
};
