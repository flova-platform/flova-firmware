#pragma once

#include <Flova.h>
#include <FlovaEsp8266Provisioning.h>
#include <adapters/ArduinoFlovaHardware.h>
#include <user_interface.h>

class FlovaEsp8266Entropy : public FlovaEntropySource {
 public:
  uint8_t byte() override { return static_cast<uint8_t>(os_random()); }
};

// Explicit ESP8266 composition. The ESP8266 package owns its filesystem,
// SoftAP, identity, and restart policy; the facade only orchestrates them.
class FlovaEsp8266 final {
 public:
  FlovaEsp8266(const FlovaClientConfig& config,
               const FlovaProvisioningConfig& provisioning = FlovaProvisioningConfig())
      : link_(entropy_), board_(storage_),
        client_(config, provisioning, link_, board_, storage_, clock_, logger_,
                entropy_, hardware_) {
    // Keep the shared hardware adapter's normalized 0..255 PWM contract.
    analogWriteRange(255);
  }

  bool begin() { return client_.begin(); }
  void run() { client_.run(); }
  bool startProvisioning() { return client_.startProvisioning(); }
  bool provisioning() const { return client_.provisioning(); }
  FlovaLifecycle lifecycle() const { return client_.lifecycle(); }
  bool connected() const { return client_.connected(); }
  bool ready() const { return client_.ready(); }
  const flova::Diagnostics& diagnostics() const { return client_.diagnostics(); }
  flova::Device& device() { return client_.device(); }

  template <typename T>
  flova::Datastream<T> datastream(const char* key) {
    return client_.datastream<T>(key);
  }

 private:
  FlovaEsp8266Entropy entropy_;
  ArduinoFlovaLink link_;
  FlovaEsp8266Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  ArduinoFlovaHardware hardware_;
  FlovaEsp8266Provisioning board_;
  FlovaClient client_;
};
