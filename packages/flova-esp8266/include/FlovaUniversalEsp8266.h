#pragma once

#include <FlovaArduino.h>
#include <FlovaEsp8266.h>
#include <FlovaEsp8266Provisioning.h>
#include <adapters/ArduinoFlovaHardware.h>

// Full-device composition used by the no-code universal firmware.
class FlovaUniversalEsp8266 final {
 public:
  FlovaUniversalEsp8266()
      : link_(entropy_), provisioning_(storage_),
        client_(link_, provisioning_, storage_, clock_, logger_, entropy_, hardware_) {
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
  bool ready() const { return client_.ready(); }
  const char* lastError() const { return client_.lastError(); }
  flova::Device& device() { return client_.device(); }

 private:
  static void scheduleRestart(void* context, FlovaRestartReason) {
    FlovaUniversalEsp8266* self = static_cast<FlovaUniversalEsp8266*>(context);
    const uint32_t now = millis();
    self->restartRequestedAt_ = now ? now : 1;
  }

  FlovaEsp8266Entropy entropy_;
  ArduinoFlovaLink link_;
  FlovaEsp8266Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  ArduinoFlovaHardware hardware_;
  FlovaEsp8266Provisioning provisioning_;
  FlovaClient client_;
  uint32_t restartRequestedAt_ = 0;
};
