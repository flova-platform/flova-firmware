#pragma once

#include <FlovaEsp32BuildProfile.h>
#include <FlovaArduino.h>
#include <FlovaEsp32.h>
#include <FlovaEsp32BleProvisioning.h>
#include <FlovaEsp32HardwarePolicy.h>
#include <adapters/ArduinoFlovaHardware.h>

#if defined(FLOVA_FIRMWARE_TARGET)
#define FLOVA_UNIVERSAL_ESP32_BLE_TARGET FLOVA_FIRMWARE_TARGET
#else
#define FLOVA_UNIVERSAL_ESP32_BLE_TARGET "universal_esp32_ble"
#endif

#if defined(FLOVA_OTA_BOOT_LAYOUT_VERSION)
#define FLOVA_UNIVERSAL_ESP32_BLE_LAYOUT FLOVA_OTA_BOOT_LAYOUT_VERSION
#else
#define FLOVA_UNIVERSAL_ESP32_BLE_LAYOUT "esp32-ab"
#endif

class FlovaUniversalEsp32Ble final {
 public:
  explicit FlovaUniversalEsp32Ble(const char* proofOfPossession = nullptr)
      : linkPlatform_(), link_(linkPlatform_, entropy_), provisioning_(proofOfPossession),
        identity_(FLOVA_UNIVERSAL_ESP32_BLE_TARGET),
        client_(link_, provisioning_, network_, tlsClock_, identity_, storage_,
                clock_, logger_, entropy_, hardware_) {
    client_.setFirmwareTarget(FLOVA_UNIVERSAL_ESP32_BLE_TARGET);
    client_.setOtaProfile(FlovaOtaStrategy::Ab, FLOVA_UNIVERSAL_ESP32_BLE_LAYOUT, true);
    client_.setOtaEnabled(true);
    client_.setRestartHandler(scheduleRestart, this);
  }

  bool begin() { return client_.begin(true); }

  void run() {
    client_.run();
    if (restartRequestedAt_ && millis() - restartRequestedAt_ >= 1000UL) {
      restartRequestedAt_ = 0;
      ESP.restart();
    }
  }

  bool provisioning() const { return client_.provisioning(); }
  FlovaLifecycle lifecycle() const { return client_.lifecycle(); }
  bool connected() const { return client_.connected(); }
  bool runtimeReady() const { return client_.runtimeReady(); }
  bool ready() const { return client_.ready(); }
  const char* lastError() const { return client_.lastError(); }
  flova::Device& device() { return client_.device(); }

 private:
  static void scheduleRestart(void* context, FlovaRestartReason) {
    FlovaUniversalEsp32Ble* self = static_cast<FlovaUniversalEsp32Ble*>(context);
    const uint32_t now = millis();
    self->restartRequestedAt_ = now ? now : 1;
  }

  FlovaEsp32Entropy entropy_;
  FlovaEsp32Platform linkPlatform_;
  ArduinoFlovaLink link_;
  FlovaEsp32Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  ArduinoFlovaHardware hardware_{flova::esp32::validInputPin,
                                 flova::esp32::validOutputPin,
                                 flova::esp32::validAnalogPin};
  FlovaEsp32BleProvisioning provisioning_;
  FlovaEsp32PlatformNetwork network_;
  ArduinoFlovaUtcBootstrap<WiFiUDP> tlsClock_;
  FlovaEsp32Identity identity_;
  FlovaClient client_;
  uint32_t restartRequestedAt_ = 0;
};
