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

// Temporary setup-channel ownership only. Runtime networking, TLS clock
// readiness, and board identity use separate service contracts.
class FlovaProvisioningAdapter {
 public:
  virtual ~FlovaProvisioningAdapter() {}
  virtual bool begin(FlovaProvisioningHandler, void*) { return true; }
  virtual void loop() {}
  virtual bool startProvisioning() { return false; }
  virtual bool stopProvisioning() { return true; }
  // Some setup transports release resources that can only be reacquired after
  // a board restart. The board composition, not the adapter, owns that restart.
  virtual bool requiresRestartBeforeProvisioning() const { return false; }
};
