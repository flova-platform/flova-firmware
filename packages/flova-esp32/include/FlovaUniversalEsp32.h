#pragma once

#include <Flova.h>
#include <FlovaEsp32.h>
#include <FlovaEsp32Provisioning.h>
#include <adapters/ArduinoFlovaHardware.h>

class FlovaUniversalEsp32 final {
 public:
  FlovaUniversalEsp32()
      : link_(entropy_), provisioning_(storage_),
        client_(link_, provisioning_, storage_, clock_, logger_, entropy_, hardware_) {
    client_.setFirmwareTarget("universal_esp32");
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
    FlovaUniversalEsp32* self = static_cast<FlovaUniversalEsp32*>(context);
    const uint32_t now = millis();
    self->restartRequestedAt_ = now ? now : 1;
  }
  FlovaEsp32Entropy entropy_;
  ArduinoFlovaLink link_;
  FlovaEsp32Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  ArduinoFlovaHardware hardware_;
  FlovaEsp32Provisioning provisioning_;
  FlovaClient client_;
  uint32_t restartRequestedAt_ = 0;
};
