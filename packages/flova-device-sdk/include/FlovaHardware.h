#pragma once

#include "FlovaConfigurationRuntime.h"

namespace flova {

class Device;

class Hardware {
 public:
  virtual ~Hardware() {}
  virtual void attach(Device& device) = 0;
  virtual bool apply(const config::Unit& unit) = 0;
  virtual void run() = 0;
  virtual void setConnected(bool connected) = 0;
};

}  // namespace flova
