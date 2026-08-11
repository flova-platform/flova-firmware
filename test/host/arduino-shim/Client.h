#pragma once

#include <stddef.h>
#include <stdint.h>

class Client {
 public:
  virtual ~Client() {}
  virtual bool connected() = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual size_t write(const uint8_t* data, size_t length) = 0;
  virtual void stop() {}
};

