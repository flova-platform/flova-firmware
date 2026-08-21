#pragma once

#include <stddef.h>

// Normal network ownership is independent from the temporary provisioning
// channel. Passive compositions observe application-owned connectivity;
// universal compositions start and stop their board-owned network here.
class FlovaNetworkRuntime {
 public:
  virtual ~FlovaNetworkRuntime() {}
  virtual bool begin() { return true; }
  virtual bool stop() { return true; }
  virtual void loop() {}
  virtual bool connected() const { return true; }
};

// TLS certificate validation may require a board-specific UTC bootstrap before
// Link can connect. It is polled separately so it cannot be hidden inside a
// provisioning or network implementation.
class FlovaTlsClockBootstrap {
 public:
  virtual ~FlovaTlsClockBootstrap() {}
  virtual void loop(bool) {}
  virtual bool ready() const { return true; }
};

// Immutable board identity defaults are configuration data, not setup-channel
// behavior. FlovaClient copies the bounded values before runtime starts.
class FlovaBoardIdentity {
 public:
  virtual ~FlovaBoardIdentity() {}
  virtual bool hardwareId(char*, size_t) const { return false; }
  virtual const char* firmwareTarget() const { return nullptr; }
};
