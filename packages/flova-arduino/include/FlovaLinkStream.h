#pragma once
#include <stddef.h>
#include <stdint.h>

// Application-facing stream operations never expose the board socket.
// Writes accept complete records into owned storage; busy is backpressure.
class FlovaLinkStream {
 public:
  virtual ~FlovaLinkStream() {}
  virtual bool connected() = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual bool writeBusy() const { return false; }
};
