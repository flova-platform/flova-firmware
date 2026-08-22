#pragma once

#include <stddef.h>

#include <FlovaDevice.h>
#include <FlovaHardware.h>
#include <FlovaLinkMessages.h>

namespace flova {
enum class OtaInstallResult : uint8_t {
  Installed,
  DownloadFailed,
  HashMismatch,
  FlashFailed,
  ResourceUnavailable
};
}  // namespace flova

// The facade needs a small bootstrap lifecycle in addition to the portable
// Link contract. Board implementations provide it; the runtime never knows
// which TLS, Wi-Fi, or socket type is underneath.
class FlovaClientLink : public flova::Link {
 public:
  virtual bool configure(const char* url, const char* deviceId,
                         const char* secret) = 0;
  virtual bool beginBootstrap(const char* url, const char* token,
                              const char* hardwareId,
                              const char* firmwareTarget,
                              const char* secret) = 0;
  virtual void pollBootstrap() = 0;
  virtual bool takeBootstrapCommitted(FlovaLinkBootstrapCommitted&) = 0;
  virtual bool takeBootstrapError(char* output, size_t capacity) = 0;
  virtual bool takeConfigurationRecord(FlovaLinkConfigurationRecord& output) = 0;
  virtual bool publishConfigurationReport(
      const FlovaLinkConfigurationReport& report) = 0;
  virtual bool publishConfigurationState(
      const FlovaLinkConfigurationState& state) = 0;
  virtual bool publishHeartbeat(const FlovaLinkHeartbeat& heartbeat) = 0;
  virtual bool publishScheduleStatus(const FlovaLinkScheduleStatus& status) = 0;
  virtual bool publishScheduleRenew(const FlovaLinkScheduleStatus& status) = 0;
  virtual bool takeOtaOffer(FlovaLinkOtaOffer& offer) = 0;
  virtual bool publishOtaReport(const FlovaLinkOtaReport& report) = 0;
  virtual flova::OtaInstallResult installOta(
      const FlovaLinkOtaOffer& offer) = 0;
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
  // Board compositions may publish their actual boot layout and recovery
  // contract. The defaults describe the portable, non-transactional updater.
  virtual void setOtaProfile(FlovaOtaStrategy, const char*, bool) {}
  virtual bool decodeStoredConfigurationRecord(
      const uint8_t* payload, size_t length,
      FlovaLinkConfigurationRecord& output) = 0;
  virtual void setConfigurationGeneration(uint32_t generation) = 0;
  virtual uint32_t configurationGeneration() const = 0;
  virtual void setHardwareCapabilities(
      const flova::HardwareCapabilities& capabilities) = 0;
  virtual bool resourceRecoveryRequired() const { return false; }
  virtual void disconnect() = 0;
};
