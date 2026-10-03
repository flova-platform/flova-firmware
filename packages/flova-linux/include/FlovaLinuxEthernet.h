#pragma once

#include <stddef.h>
#include <stdint.h>

namespace flova_linux {

// Linux owns Ethernet, DHCP, routing, and certificate storage. Applications
// provide the connected TLS/WebSocket socket and adapt it to flova::Link.
class EthernetSocket {
 public:
  virtual ~EthernetSocket() {}
  virtual bool begin() { return true; }
  virtual bool open(const char* host, uint16_t port) = 0;
  virtual bool connected() const = 0;
  virtual size_t read(uint8_t* output, size_t capacity) = 0;
  virtual size_t write(const uint8_t* data, size_t length) = 0;
  virtual void close() = 0;
  virtual const char* error() const { return "ethernet_link_failed"; }
};

}  // namespace flova_linux
