#pragma once

#include <stddef.h>
#include <stdint.h>

#include <Client.h>

#include <FlovaClientLink.h>

enum class FlovaLinkOpenStatus : uint8_t {
  InProgress,
  Connected,
  Failed
};

// Board policy for the bounded Arduino Device Link core. The core owns the
// protocol queues and WebSocket parser; a board owns the concrete client,
// TLS/resource setup, non-blocking write behavior, and OTA implementation.
class FlovaArduinoPlatform {
 public:
  virtual ~FlovaArduinoPlatform() {}
  virtual Client& linkClient() = 0;
  virtual bool beginLink() { return true; }
  virtual bool startLink(const char* host, uint16_t port) = 0;
  virtual FlovaLinkOpenStatus pollLink() = 0;
  virtual void closeLink() = 0;
  virtual bool resourceRecoveryRequired() const { return false; }
  virtual bool linkWriteBusy() const { return false; }
  virtual bool submitLinkWrite(const uint8_t* data, size_t length) = 0;
  virtual bool serviceLinkWrite() { return true; }
  virtual uint32_t otaMaxImageBytes() const { return 0; }
  virtual FlovaOtaStrategy otaStrategy() const {
    return FlovaOtaStrategy::None;
  }
  virtual const char* otaBootLayoutVersion() const { return "legacy"; }
  virtual bool otaRollbackCapable() const { return false; }
  virtual FlovaOtaBootState otaBootState() const {
    return FlovaOtaBootState::Stable;
  }
  virtual bool confirmOtaBoot() { return false; }
  virtual bool rollbackOtaBoot() { return false; }
  virtual flova::OtaInstallResult installOta(
      const FlovaLinkOtaOffer&) = 0;
};
