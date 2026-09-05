#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_err.h>
#include <protocomm.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

#include <FlovaProvisioningAdapter.h>
#include <FlovaWifiProvisioning.h>

namespace flova {
namespace esp32 {
namespace detail {

// Forces the package's Arduino BLE compatibility object into the final image
// when this adapter is used. The object supplies the framework hook before
// initArduino() releases Bluetooth memory.
void ensureBleArduinoSupport();

}  // namespace detail
}  // namespace esp32
}  // namespace flova

// ESP-IDF's provisioning manager owns the BLE transport and its internal
// buffers. This adapter only retains bounded application records and lets
// FlovaClient perform durable handoff acceptance from the cooperative loop.
class FlovaEsp32BleProvisioning final : public FlovaProvisioningAdapter {
 public:
  explicit FlovaEsp32BleProvisioning(const char* proofOfPossession = nullptr) {
    flova::esp32::detail::ensureBleArduinoSupport();
    if (proofOfPossession) {
      strncpy(pop_, proofOfPossession, sizeof(pop_) - 1);
      pop_[sizeof(pop_) - 1] = 0;
    }
  }

  bool begin(FlovaProvisioningHandler handler, void* context) override {
    handler_ = handler;
    context_ = context;
    return handler_ != nullptr;
  }

  void loop() override {
    if (managerStarted_ && credentialFailurePending_) {
      // WiFiGeneric may have started a reconnect from its STA_DISCONNECTED
      // callback. Cancel that attempt from the cooperative loop before
      // resetting the provisioning manager, rather than changing Wi-Fi state
      // re-entrantly from the provisioning callback.
      credentialFailurePending_ = false;
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false, false);
      if (wifi_prov_mgr_reset_sm_state_on_failure() != ESP_OK)
        copyError("wifi_reset_failed");
    }

    if (!managerStarted_ || !handoffReady_ || !credentialsReady_ || attempted_)
      return;

    // Give the phone's provisioning library time to receive the successful
    // Wi-Fi response before tearing down its BLE transport.
    if (credentialSuccessPending_ &&
        millis() - credentialSuccessAtMs_ < kCredentialSuccessGraceMs)
      return;
    credentialSuccessPending_ = false;

    attempted_ = true;
    const FlovaProvisioningResponse result = handler_(context_, handoff_);
    if (result == FlovaProvisioningResponse::Accepted) {
      accepted_ = true;
      lastError_[0] = 0;
    } else if (result == FlovaProvisioningResponse::StorageFailed) {
      accepted_ = false;
      copyError("storage_failed");
    } else {
      accepted_ = false;
      copyError("invalid_handoff");
    }
  }

  bool startProvisioning() override {
    if (managerStarted_) return true;
    if (bleMemoryReleased_) {
      copyError("ble_restart_required");
      return false;
    }

    autoReconnectBeforeProvisioning_ = WiFi.getAutoReconnect();
    autoReconnectSaved_ = true;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_STA);
    handoffReady_ = false;
    credentialsReady_ = false;
    credentialFailurePending_ = false;
    credentialSuccessPending_ = false;
    attempted_ = false;
    accepted_ = false;
    lastError_[0] = 0;

    wifi_prov_mgr_config_t config = {};
    config.scheme = wifi_prov_scheme_ble;
    config.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    config.app_event_handler.event_cb = provisioningEvent;
    config.app_event_handler.user_data = this;
    if (wifi_prov_mgr_init(config) != ESP_OK) {
      restoreAutoReconnect();
      copyError("ble_manager_init_failed");
      return false;
    }
    managerInitialized_ = true;

    if (wifi_prov_mgr_endpoint_create(kHandoffEndpoint) != ESP_OK ||
        wifi_prov_mgr_endpoint_create(kStatusEndpoint) != ESP_OK ||
        wifi_prov_mgr_disable_auto_stop(1000) != ESP_OK) {
      wifi_prov_mgr_deinit();
      managerInitialized_ = false;
      restoreAutoReconnect();
      copyError("ble_endpoint_init_failed");
      return false;
    }

    const char* capabilities[] = {kHandoffEndpoint, kStatusEndpoint};
    wifi_prov_mgr_set_app_info("flova", FLOVA_FIRMWARE_VERSION, capabilities,
                               sizeof(capabilities) / sizeof(capabilities[0]));

    char serviceName[24] = {};
    snprintf(serviceName, sizeof(serviceName), "Flova-BLE-%06lX",
             static_cast<unsigned long>(ESP.getEfuseMac() & 0xffffffUL));
    if (wifi_prov_mgr_start_provisioning(
            WIFI_PROV_SECURITY_1, pop_[0] ? pop_ : nullptr, serviceName,
            nullptr) != ESP_OK) {
      wifi_prov_mgr_deinit();
      managerInitialized_ = false;
      restoreAutoReconnect();
      copyError("ble_start_failed");
      return false;
    }

    if (wifi_prov_mgr_endpoint_register(kHandoffEndpoint, handleHandoff,
                                        this) != ESP_OK ||
        wifi_prov_mgr_endpoint_register(kStatusEndpoint, handleStatus,
                                        this) != ESP_OK) {
      wifi_prov_mgr_deinit();
      managerInitialized_ = false;
      restoreAutoReconnect();
      copyError("ble_endpoint_register_failed");
      return false;
    }

    managerStarted_ = true;
    return true;
  }

  bool stopProvisioning() override {
    if (managerInitialized_) {
      wifi_prov_mgr_stop_provisioning();
      wifi_prov_mgr_wait();
      wifi_prov_mgr_deinit();
      bleMemoryReleased_ = true;
      managerInitialized_ = false;
      managerStarted_ = false;
    }
    credentialFailurePending_ = false;
    credentialSuccessPending_ = false;
    restoreAutoReconnect();
    return true;
  }

  bool requiresRestartBeforeProvisioning() const override {
    return bleMemoryReleased_;
  }

 private:
  static const uint32_t kCredentialSuccessGraceMs = 1500UL;
  static constexpr const char* kHandoffEndpoint = "flova-handoff";
  static constexpr const char* kStatusEndpoint = "flova-status";

  static void provisioningEvent(void* context, wifi_prov_cb_event_t event,
                                void* eventData) {
    FlovaEsp32BleProvisioning* self =
        static_cast<FlovaEsp32BleProvisioning*>(context);
    if (!self) return;

    if (event == WIFI_PROV_CRED_RECV) {
      // A new credential submission supersedes the previous retryable error.
      // The manager owns the actual station attempt and reports its result
      // through the following CRED_SUCCESS/CRED_FAIL event.
      self->credentialsReady_ = false;
      self->credentialFailurePending_ = false;
      self->credentialSuccessPending_ = false;
      self->lastError_[0] = 0;
    } else if (event == WIFI_PROV_CRED_SUCCESS) {
      self->credentialsReady_ = true;
      self->credentialFailurePending_ = false;
      self->credentialSuccessAtMs_ = millis();
      self->credentialSuccessPending_ = true;
    } else if (event == WIFI_PROV_CRED_FAIL) {
      self->credentialsReady_ = false;
      const char* error = "wifi_failed";
      if (eventData) {
        const wifi_prov_sta_fail_reason_t reason =
            *static_cast<const wifi_prov_sta_fail_reason_t*>(eventData);
        if (reason == WIFI_PROV_STA_AP_NOT_FOUND) {
          error = "wifi_ap_not_found";
        } else if (reason == WIFI_PROV_STA_AUTH_ERROR) {
          error = "wifi_auth_failed";
        }
      }
      self->copyError(error);
      self->credentialSuccessPending_ = false;
      // Defer the reset until loop() so the manager and Arduino Wi-Fi event
      // callbacks cannot mutate the STA state re-entrantly. The reset clears
      // the failed credentials while retaining the BLE provisioning session.
      self->credentialFailurePending_ = true;
    }
  }

  static esp_err_t handleHandoff(uint32_t, const uint8_t* input, ssize_t length,
                                 uint8_t** output, ssize_t* outputLength,
                                 void* context) {
    FlovaEsp32BleProvisioning* self =
        static_cast<FlovaEsp32BleProvisioning*>(context);
    if (!self || !input || length <= 0 ||
        static_cast<size_t>(length) >= sizeof(self->input_))
      return ESP_ERR_INVALID_ARG;

    memcpy(self->input_, input, static_cast<size_t>(length));
    self->input_[length] = 0;
    flova::ProvisioningHandoff handoff;
    if (!flova::parseWifiProvisioningHandoff(
            self->input_, static_cast<size_t>(length), handoff)) {
      self->copyError("invalid_handoff");
      return makeResponse("{\"ok\":false,\"error\":\"invalid_handoff\"}",
                          output, outputLength);
    }

    self->handoff_ = handoff;
    self->handoffReady_ = true;
    self->attempted_ = false;
    self->accepted_ = false;
    self->lastError_[0] = 0;
    return makeResponse("{\"ok\":true,\"status\":\"received\"}", output,
                        outputLength);
  }

  static esp_err_t handleStatus(uint32_t, const uint8_t*, ssize_t, uint8_t** output,
                                ssize_t* outputLength, void* context) {
    FlovaEsp32BleProvisioning* self =
        static_cast<FlovaEsp32BleProvisioning*>(context);
    if (!self) return ESP_ERR_INVALID_ARG;
    char response[160] = {};
    if (self->accepted_) {
      strcpy(response, "{\"status\":\"accepted\"}");
    } else if (self->lastError_[0]) {
      snprintf(response, sizeof(response),
               "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"can_retry\":true,\"last_error_code\":\"%s\"}",
               self->lastError_);
    } else {
      strcpy(response,
             "{\"status\":\"setup_mode\",\"protocol\":\"flova-link-v1\",\"can_retry\":true}");
    }
    return makeResponse(response, output, outputLength);
  }

  static esp_err_t makeResponse(const char* value, uint8_t** output,
                                ssize_t* outputLength) {
    if (!value || !output || !outputLength) return ESP_ERR_INVALID_ARG;
    const size_t length = strlen(value);
    // protocomm owns and releases endpoint response buffers after dispatch;
    // keep this allocation limited to the bounded setup response path.
    uint8_t* buffer = static_cast<uint8_t*>(malloc(length));
    if (!buffer) return ESP_ERR_NO_MEM;
    memcpy(buffer, value, length);
    *output = buffer;
    *outputLength = static_cast<ssize_t>(length);
    return ESP_OK;
  }

  void copyError(const char* value) {
    if (!value) return;
    strncpy(lastError_, value, sizeof(lastError_) - 1);
    lastError_[sizeof(lastError_) - 1] = 0;
  }

  void restoreAutoReconnect() {
    if (!autoReconnectSaved_) return;
    WiFi.setAutoReconnect(autoReconnectBeforeProvisioning_);
    autoReconnectSaved_ = false;
  }

  FlovaProvisioningHandler handler_ = nullptr;
  void* context_ = nullptr;
  char pop_[65] = {};
  char input_[768] = {};
  char lastError_[40] = {};
  flova::ProvisioningHandoff handoff_;
  bool managerInitialized_ = false;
  bool managerStarted_ = false;
  bool bleMemoryReleased_ = false;
  bool handoffReady_ = false;
  bool credentialsReady_ = false;
  bool credentialFailurePending_ = false;
  bool credentialSuccessPending_ = false;
  uint32_t credentialSuccessAtMs_ = 0;
  bool attempted_ = false;
  bool accepted_ = false;
  bool autoReconnectBeforeProvisioning_ = true;
  bool autoReconnectSaved_ = false;
};
