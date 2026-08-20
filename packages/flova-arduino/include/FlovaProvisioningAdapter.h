#pragma once

#include <stddef.h>
#include <stdint.h>

#include <FlovaDevice.h>
#include <FlovaConfiguration.h>

enum class FlovaProvisioningResponse : uint8_t {
  Invalid,
  Accepted,
  StorageFailed,
};

typedef FlovaProvisioningResponse (*FlovaProvisioningHandler)(
    void* context, const flova::ProvisioningHandoff& input);

// Board runtime prerequisites and an optional setup-channel owner. The normal
// SDK implementation only observes connectivity; universal firmware supplies
// the concrete SoftAP owner.
class FlovaProvisioningAdapter {
 public:
  virtual ~FlovaProvisioningAdapter() {}
  virtual bool begin(FlovaProvisioningHandler, void*) { return true; }
  virtual void loop() {}
  virtual bool startProvisioning() { return false; }
  virtual bool beginRuntime() { return true; }
  virtual bool runtimeConnected() const { return true; }
  virtual bool clockReady() const { return true; }
  virtual bool defaultHardwareId(char*, size_t) const { return false; }
  virtual const char* defaultFirmwareTarget() const { return nullptr; }
};
