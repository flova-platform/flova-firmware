#pragma once

#include <ESP8266WebServer.h>
#include <user_interface.h>

#include <FlovaArduino.h>
#include <FlovaEsp8266Platform.h>
#include <FlovaEsp8266Services.h>
#include <FlovaWifiProvisioning.h>
#include <adapters/ArduinoFlovaLink.h>
#include <adapters/ArduinoFlovaManualHardware.h>

class FlovaEsp8266Entropy : public FlovaEntropySource {
 public:
  uint8_t byte() override { return static_cast<uint8_t>(os_random()); }
};

// Plug-in facade for existing applications. It owns only Flova-private state
// and observes the application's connectivity without changing it.
class FlovaEsp8266 final {
 public:
  FlovaEsp8266()
      : linkPlatform_(), link_(linkPlatform_, entropy_),
        identity_("custom_arduino_esp8266"),
        client_(link_, provisioning_, network_, tlsClock_, identity_, storage_,
                clock_, logger_, entropy_, hardware_) {}

  bool begin() { return client_.begin(false); }
  void run() { client_.run(); }

  FlovaProvisioningResponse provision(const flova::ProvisioningHandoff& input) {
    return client_.provision(input);
  }

  bool attachProvisioning(ESP8266WebServer& server) {
    if (routesAttached_) return false;
    provisioningServer_ = &server;
    server.on("/status", HTTP_GET, [this]() {
      const char* error = lastError();
      snprintf(response_, sizeof(response_),
               error && error[0]
                   ? "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"can_retry\":true,\"last_error_code\":\"%s\"}"
                   : "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"can_retry\":true}",
               error ? error : "");
      provisioningServer_->send(200, "application/json", response_);
    });
    server.on("/provision", HTTP_POST, [this]() {
      const String& body = provisioningServer_->arg("plain");
      if (body.length() >= 768 ||
          !flova::parseWifiProvisioningHandoff(body.c_str(), body.length(),
                                               provisioningInput_)) {
        provisioningServer_->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"invalid_handoff\"}");
        return;
      }
      const FlovaProvisioningResponse result = provision(provisioningInput_);
      if (result == FlovaProvisioningResponse::Accepted)
        provisioningServer_->send(202, "application/json", "{\"ok\":true,\"status\":\"accepted\"}");
      else if (result == FlovaProvisioningResponse::StorageFailed)
        provisioningServer_->send(500, "application/json", "{\"ok\":false,\"error\":\"storage_failed\"}");
      else
        provisioningServer_->send(409, "application/json", "{\"ok\":false,\"error\":\"invalid_state\"}");
    });
    routesAttached_ = true;
    return true;
  }

  bool startProvisioning() { return client_.startProvisioning(); }
  bool provisioning() const { return client_.provisioning(); }
  FlovaLifecycle lifecycle() const { return client_.lifecycle(); }
  bool connected() const { return client_.connected(); }
  bool ready() const { return client_.ready(); }
  const char* lastError() const { return client_.lastError(); }
  const flova::Diagnostics& diagnostics() const { return client_.diagnostics(); }
  flova::Device& device() { return client_.device(); }
  bool setFirmwareTarget(const char* target) { return client_.setFirmwareTarget(target); }
  void enableOta(bool enabled = true) { client_.setOtaEnabled(enabled); }
  void setOtaProfile(FlovaOtaStrategy strategy, const char* bootLayoutVersion,
                     bool rollbackCapable = false) {
    client_.setOtaProfile(strategy, bootLayoutVersion, rollbackCapable);
  }
  void setRestartHandler(FlovaRestartHandler handler, void* context = nullptr) {
    client_.setRestartHandler(handler, context);
  }
  bool restartRequired() const { return client_.restartRequired(); }
  FlovaRestartReason restartReason() const { return client_.restartReason(); }

  template <typename T>
  flova::Datastream<T> datastream(const char* key) {
    return client_.datastream<T>(key);
  }

 private:
  FlovaEsp8266Entropy entropy_;
  FlovaEsp8266Platform linkPlatform_;
  ArduinoFlovaLink link_;
  FlovaEsp8266Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  ArduinoFlovaManualHardware hardware_;
  FlovaProvisioningAdapter provisioning_;
  FlovaEsp8266ObservedNetwork network_;
  ArduinoFlovaUtcBootstrap<WiFiUDP> tlsClock_;
  FlovaEsp8266Identity identity_;
  FlovaClient client_;
  ESP8266WebServer* provisioningServer_ = nullptr;
  flova::ProvisioningHandoff provisioningInput_;
  char response_[192] = {};
  bool routesAttached_ = false;
};
