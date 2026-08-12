#pragma once

#include <FlovaCore.h>
#include <FlovaHardware.h>

// The developer SDK never claims GPIO from cloud configuration. Applications
// bind their own hardware through datastream callbacks.
class ArduinoFlovaApplicationHardware final : public flova::Hardware {
 public:
  void attach(flova::Device&) override {}

  bool validate(const flova::config::Unit& unit) override {
    if (unit.kind == flova::config::UnitKind::System)
      return !unit.data.system.hasStatusLedPin;
    return unit.kind != flova::config::UnitKind::Datastream ||
           !unit.data.datastream.hasMapping;
  }

  bool apply(const flova::config::Unit& unit) override {
    return validate(unit);
  }

  void run() override {}
  void setConnected(bool) override {}
};
