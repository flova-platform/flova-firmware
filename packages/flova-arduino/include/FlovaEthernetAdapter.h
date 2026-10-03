#pragma once

#include <stddef.h>
#include <stdint.h>

#include <FlovaArduinoPlatform.h>

// Custom applications provide the concrete Ethernet/TLS client. Chip drivers,
// SPI pins, and PHY policy stay outside the SDK compositions.
class FlovaEthernetSocket {
 public:
  virtual ~FlovaEthernetSocket() {}
  virtual bool begin() { return true; }
  virtual bool open(const char* host, uint16_t port) = 0;
  virtual bool connected() = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual size_t write(const uint8_t* data, size_t length) = 0;
  virtual void close() = 0;
  virtual const char* error() const { return "ethernet_link_failed"; }
};

class FlovaEthernetPlatform final : public FlovaArduinoPlatform {
 public:
  explicit FlovaEthernetPlatform(FlovaEthernetSocket& socket)
      : socket_(socket), opening_(false) {}

  bool beginLink() override { return socket_.begin(); }
  bool connected() override { return socket_.connected(); }
  int available() override { return socket_.available(); }
  int read() override { return socket_.read(); }
  bool linkClosed() const override {
    return !const_cast<FlovaEthernetSocket&>(socket_).connected();
  }
  bool startLink(const char* host, uint16_t port) override {
    opening_ = true;
    return socket_.open(host, port);
  }
  FlovaLinkOpenStatus pollLink() override {
    if (socket_.connected()) {
      opening_ = false;
      return FlovaLinkOpenStatus::Connected;
    }
    return opening_ ? FlovaLinkOpenStatus::InProgress
                    : FlovaLinkOpenStatus::Failed;
  }
  void closeLink() override {
    opening_ = false;
    socket_.close();
  }
  const char* linkError() const override { return socket_.error(); }
  size_t write(const uint8_t* data, size_t length) override {
    return socket_.write(data, length);
  }
  bool submitLinkWrite(const uint8_t* data, size_t length) override {
    return socket_.write(data, length) == length;
  }
  flova::OtaInstallResult installOta(
      const FlovaLinkOtaOffer&) override {
    return flova::OtaInstallResult::ResourceUnavailable;
  }

 private:
  FlovaEthernetSocket& socket_;
  bool opening_;
};
