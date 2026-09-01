#pragma once

#include <FlovaEsp32.h>
#include <FlovaEsp32BleProvisioning.h>

#if defined(FLOVA_FIRMWARE_TARGET)
#define FLOVA_ESP32_BLE_TARGET FLOVA_FIRMWARE_TARGET
#else
#define FLOVA_ESP32_BLE_TARGET "custom_arduino_esp32_ble"
#endif

#if defined(FLOVA_OTA_BOOT_LAYOUT_VERSION)
#define FLOVA_ESP32_BLE_LAYOUT FLOVA_OTA_BOOT_LAYOUT_VERSION
#else
#define FLOVA_ESP32_BLE_LAYOUT "esp32-ab"
#endif

class FlovaEsp32Ble final {
 public:
  FlovaEsp32Ble()
      : linkPlatform_(), link_(linkPlatform_, entropy_),
        identity_(FLOVA_ESP32_BLE_TARGET),
    client_(link_, provisioning_, network_, tlsClock_, identity_, storage_,
                clock_, logger_, entropy_, hardware_) {
    client_.setFirmwareTarget(FLOVA_ESP32_BLE_TARGET);
    client_.setOtaProfile(FlovaOtaStrategy::Ab, FLOVA_ESP32_BLE_LAYOUT, true);
    client_.setOtaEnabled(true);
  }

  bool begin() { return client_.begin(true); }
  void run() { client_.run(); }
  bool provisioning() const { return client_.provisioning(); }
  FlovaLifecycle lifecycle() const { return client_.lifecycle(); }
  bool connected() const { return client_.connected(); }
  bool runtimeReady() const { return client_.runtimeReady(); }
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
  FlovaEsp32Entropy entropy_;
  FlovaEsp32Platform linkPlatform_;
  ArduinoFlovaLink link_;
  FlovaEsp32Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  FlovaEsp32BleProvisioning provisioning_;
  FlovaEsp32PlatformNetwork network_;
  ArduinoFlovaUtcBootstrap<WiFiUDP> tlsClock_;
  FlovaEsp32Identity identity_;
  ArduinoFlovaManualHardware hardware_;
  FlovaClient client_;
};
