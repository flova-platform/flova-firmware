#pragma once

#include <FlovaArduino.h>
#include <FlovaEsp32.h>
#include <FlovaEsp32BleProvisioning.h>
#include <adapters/ArduinoFlovaHardware.h>

class FlovaUniversalEsp32Ble final {
 public:
  explicit FlovaUniversalEsp32Ble(const char* proofOfPossession = nullptr)
      : link_(entropy_), provisioning_(proofOfPossession),
        identity_("universal_esp32_ble"),
        client_(link_, provisioning_, network_, tlsClock_, identity_, storage_,
                clock_, logger_, entropy_, hardware_) {
    client_.setFirmwareTarget("universal_esp32_ble");
    // The BLE/BTDM image does not fit either 1.28 MiB OTA slot on the 4 MiB
    // DevKit. This MVP uses the 2 MiB single-app partition; production OTA
    // needs a larger-flash board or a deliberately larger partition layout.
    client_.setOtaEnabled(false);
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
  ArduinoFlovaLink link_;
  FlovaEsp32Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  ArduinoFlovaHardware hardware_;
  FlovaEsp32BleProvisioning provisioning_;
  FlovaEsp32PlatformNetwork network_;
  ArduinoFlovaUtcBootstrap<WiFiUDP> tlsClock_;
  FlovaEsp32Identity identity_;
  FlovaClient client_;
  uint32_t restartRequestedAt_ = 0;
};
