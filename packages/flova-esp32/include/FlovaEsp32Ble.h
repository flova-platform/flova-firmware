#pragma once

#include <FlovaEsp32.h>
#include <FlovaEsp32BleProvisioning.h>

class FlovaEsp32Ble final {
 public:
  FlovaEsp32Ble()
      : link_(entropy_),
        identity_("custom_arduino_esp32_ble"),
        client_(link_, provisioning_, network_, tlsClock_, identity_, storage_,
                clock_, logger_, entropy_, hardware_) {
    client_.setFirmwareTarget("custom_arduino_esp32_ble");
    client_.setOtaEnabled(false);
  }

  bool begin() { return client_.begin(true); }
  void run() { client_.run(); }
  bool provisioning() const { return client_.provisioning(); }
  bool connected() const { return client_.connected(); }
  bool ready() const { return client_.ready(); }
  const char* lastError() const { return client_.lastError(); }
  flova::Device& device() { return client_.device(); }

  template <typename T>
  flova::Datastream<T> datastream(const char* key) {
    return client_.datastream<T>(key);
  }

 private:
  FlovaEsp32Entropy entropy_;
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
