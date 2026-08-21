#pragma once

#include <FlovaHardware.h>

// Custom applications own their pins. This policy keeps cloud configuration
// from silently claiming hardware that the application controls explicitly.
class ArduinoFlovaManualHardware final : public flova::Hardware {
 public:
  flova::HardwareCapabilities capabilities() const override {
    return flova::HardwareCapabilities();
  }

  void attach(flova::Device&) override {}

  // Mappings may exist in a template shared with universal firmware. They are
  // accepted for backward compatibility but never applied by this adapter.
  bool validate(const flova::config::Unit&) override { return true; }

  bool apply(const flova::config::Unit&) override { return true; }

  void resetConfiguration() override {}
  void run() override {}
  void setConnected(bool) override {}
};
