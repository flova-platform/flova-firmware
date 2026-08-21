#pragma once

#include "FlovaConfigurationRuntime.h"

namespace flova {

class Device;

struct HardwareCapabilities {
  bool automaticMapping;
  uint16_t inputSlots;
  uint16_t outputSlots;
  bool statusIndicator;

  HardwareCapabilities(bool automatic = false, uint16_t inputs = 0,
                       uint16_t outputs = 0, bool indicator = false)
      : automaticMapping(automatic),
        inputSlots(inputs),
        outputSlots(outputs),
        statusIndicator(indicator) {}
};

class Hardware {
 public:
  virtual ~Hardware() {}
  virtual void attach(Device& device) = 0;
  virtual HardwareCapabilities capabilities() const {
    return HardwareCapabilities();
  }
  virtual bool validate(const config::Unit&) { return true; }
  virtual bool apply(const config::Unit& unit) = 0;
  virtual void resetConfiguration() {}
  virtual void failSafe() {}
  virtual void run() = 0;
  virtual void setConnected(bool connected) = 0;
  virtual const char* configurationError() const { return ""; }
};

}  // namespace flova
