#pragma once

#include <stddef.h>
#include <stdint.h>

#include <FlovaCore.h>

enum class FlovaProvisioningResponse : uint8_t {
  Invalid,
  Accepted,
  StorageFailed,
};

typedef FlovaProvisioningResponse (*FlovaProvisioningHandler)(void* context,
                                                               const char* body,
                                                               size_t length);

// Board packages implement this small lifecycle seam. WebServer, Wi-Fi, and
// durable storage stay outside the portable core and outside the Arduino Link
// adapter. The handler is only given a bounded request body.
class FlovaBoardProvisioning {
 public:
  virtual ~FlovaBoardProvisioning() {}
  virtual bool beginStorage() { return true; }
  virtual bool begin(FlovaProvisioningHandler handler, void* context) = 0;
  virtual void loop() = 0;
  virtual bool startProvisioning() = 0;
  virtual bool provisioning() const = 0;
  virtual bool beginStation(const char* ssid, const char* password) = 0;
  virtual bool stationConnected() const = 0;
  virtual bool clockReady() const = 0;
  virtual void scheduleRestart() = 0;
  virtual bool defaultHardwareId(char*, size_t) const { return false; }
  virtual const char* defaultFirmwareTarget() const { return nullptr; }
};

class ArduinoFlovaNoProvisioning : public FlovaBoardProvisioning {
 public:
  explicit ArduinoFlovaNoProvisioning(flova::Storage&) {}
  bool beginStorage() override { return true; }
  bool begin(FlovaProvisioningHandler, void*) override { return true; }
  void loop() override {}
  bool startProvisioning() override { return false; }
  bool provisioning() const override { return false; }
  bool beginStation(const char*, const char*) override { return false; }
  bool stationConnected() const override { return true; }
  bool clockReady() const override { return true; }
  void scheduleRestart() override {}
};
