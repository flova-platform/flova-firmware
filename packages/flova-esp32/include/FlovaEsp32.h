#pragma once

#include <Flova.h>
#include <FlovaEsp32Provisioning.h>
#include <adapters/ArduinoFlovaHardware.h>
#include <esp_system.h>

class FlovaEsp32Entropy : public FlovaEntropySource {
 public:
  uint8_t byte() override { return static_cast<uint8_t>(esp_random()); }
};

// Explicit ESP32 composition. Board selection happens at the include site,
// so the shared Arduino facade never guesses a platform with preprocessor
// branches.
class FlovaEsp32 final {
 public:
  FlovaEsp32(const FlovaClientConfig& config,
             const FlovaProvisioningConfig& provisioning = FlovaProvisioningConfig())
      : link_(entropy_), board_(storage_),
        client_(config, provisioning, link_, board_, storage_, clock_, logger_,
                entropy_, hardware_) {}

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
  FlovaEsp32Entropy entropy_;
  ArduinoFlovaLink link_;
  FlovaEsp32Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  ArduinoFlovaHardware hardware_;
  FlovaEsp32Provisioning board_;
  FlovaClient client_;
};
