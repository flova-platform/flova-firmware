#pragma once

#include <FlovaEsp8266BuildProfile.h>
#include <FlovaArduino.h>
#include <FlovaEsp8266.h>
#include <FlovaEsp8266Provisioning.h>
#include <FlovaEsp8266HardwarePolicy.h>
#include <adapters/ArduinoFlovaHardware.h>

// Full-device composition used by the no-code universal firmware.
class FlovaUniversalEsp8266 final {
 public:
  explicit FlovaUniversalEsp8266(const char* setupPassword = nullptr)
      : linkPlatform_(), link_(linkPlatform_, entropy_),
        provisioning_(storage_, setupPassword),
        network_(storage_), identity_("universal_esp8266"),
        client_(link_, provisioning_, network_, tlsClock_, identity_, storage_,
                clock_, logger_, entropy_, hardware_) {
    analogWriteRange(255);
    client_.setFirmwareTarget("universal_esp8266");
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
  bool factoryReset() { return client_.factoryReset(); }

  template <typename T>
  flova::Datastream<T> datastream(const char* key) {
    return client_.datastream<T>(key);
  }

 private:
  static void scheduleRestart(void* context, FlovaRestartReason) {
    FlovaUniversalEsp8266* self = static_cast<FlovaUniversalEsp8266*>(context);
    const uint32_t now = millis();
    self->restartRequestedAt_ = now ? now : 1;
  }

  FlovaEsp8266Entropy entropy_;
  FlovaEsp8266Platform linkPlatform_;
  ArduinoFlovaLink link_;
  FlovaEsp8266Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  ArduinoFlovaHardware hardware_{flova::esp8266::validDigitalPin,
                                 flova::esp8266::validDigitalPin,
                                 flova::esp8266::validAnalogPin};
  FlovaEsp8266Provisioning provisioning_;
  FlovaEsp8266StoredNetwork network_;
  ArduinoFlovaUtcBootstrap<WiFiUDP> tlsClock_;
  FlovaEsp8266Identity identity_;
  FlovaClient client_;
  uint32_t restartRequestedAt_ = 0;
};
