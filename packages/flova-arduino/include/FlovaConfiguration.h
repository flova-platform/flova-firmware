#pragma once

#include <stdint.h>
#include <string.h>

namespace flova {

// Device credentials are local storage metadata.  The Engine configuration is
// never serialized here: CONFIG_BEGIN/RECORD/END is decoded into one bounded
// record and committed through the A/B installer.
static const size_t kDeviceIdBytes = 97;
static const size_t kLinkUrlBytes = 193;
static const size_t kSecretTextBytes = 65;
static const size_t kWifiSsidBytes = 33;
static const size_t kWifiPasswordBytes = 65;
static const size_t kTemplateIdBytes = 37;
static const size_t kChecksumTextBytes = 65;
static const size_t kProvisionTokenBytes = 65;
static const size_t kProvisioningErrorBytes = 48;
static const uint16_t kConfigurationImageVersion = 1;

struct DeviceConfiguration {
  char wifiSsid[kWifiSsidBytes] = {};
  char wifiPassword[kWifiPasswordBytes] = {};
  char deviceId[kDeviceIdBytes] = {};
  char linkUrl[kLinkUrlBytes] = {};
  char linkSecret[kSecretTextBytes] = {};
  char templateVersionId[kTemplateIdBytes] = {};
  char checksum[kChecksumTextBytes] = {};
  uint32_t generation = 0;
};

struct ProvisioningHandoff {
  char wifiSsid[kWifiSsidBytes] = {};
  char wifiPassword[kWifiPasswordBytes] = {};
  char linkUrl[kLinkUrlBytes] = {};
  char token[kProvisionTokenBytes] = {};
  char linkSecret[kSecretTextBytes] = {};
};

struct ProvisioningHandoffImage {
  uint16_t version;
  ProvisioningHandoff handoff;
  uint8_t attempts;
  uint8_t inProgress;
  char lastError[kProvisioningErrorBytes];
  uint32_t checksum;
};

enum class ProvisioningBootMode : uint8_t {
  Setup,
  Bootstrap,
  InterruptedBootstrap,
  Runtime,
};

inline ProvisioningBootMode provisioningBootMode(bool hasCredentials,
                                                  bool hasPendingHandoff,
                                                  bool handoffInProgress) {
  if (hasCredentials) return ProvisioningBootMode::Runtime;
  if (!hasPendingHandoff) return ProvisioningBootMode::Setup;
  return handoffInProgress ? ProvisioningBootMode::InterruptedBootstrap
                           : ProvisioningBootMode::Bootstrap;
}

template <size_t N>
inline bool copyBounded(const char* value, char (&out)[N], bool required = false) {
  const size_t length = value ? strnlen(value, N) : 0;
  if (length >= N || (required && length == 0)) return false;
  if (length) memcpy(out, value, length);
  out[length] = 0;
  return true;
}

inline bool configurationValid(const DeviceConfiguration& config) {
  return config.wifiSsid[0] && config.deviceId[0] &&
         strncmp(config.linkUrl, "wss://", 6) == 0 && config.linkSecret[0];
}

inline uint32_t provisioningChecksum(const ProvisioningHandoff& handoff) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&handoff);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < sizeof(handoff); ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

inline bool provisioningValid(const ProvisioningHandoff& handoff) {
  return handoff.wifiSsid[0] && handoff.token[0] && handoff.linkSecret[0] &&
         strncmp(handoff.linkUrl, "wss://", 6) == 0;
}

inline void makeProvisioningImage(const ProvisioningHandoff& handoff,
                                   ProvisioningHandoffImage& image) {
  image = ProvisioningHandoffImage();
  image.version = kConfigurationImageVersion;
  image.handoff = handoff;
  image.checksum = provisioningChecksum(handoff);
}

inline bool verifyProvisioningImage(const ProvisioningHandoffImage& image) {
  return image.version == kConfigurationImageVersion &&
         provisioningValid(image.handoff) &&
         image.checksum == provisioningChecksum(image.handoff);
}

inline void markProvisioningAttempt(ProvisioningHandoffImage& image) {
  if (image.attempts < 255) ++image.attempts;
  image.inProgress = 1;
  image.lastError[0] = 0;
}

template <size_t N>
inline void sanitizeProvisioningError(const char* input, char (&output)[N]);

inline void markProvisioningFailure(ProvisioningHandoffImage& image, const char* code) {
  image.inProgress = 0;
  sanitizeProvisioningError(code, image.lastError);
}

template <size_t N>
inline void sanitizeProvisioningError(const char* input, char (&output)[N]) {
  if (!N) return;
  size_t cursor = 0;
  for (const char* value = input; value && *value && cursor + 1 < N; ++value) {
    const char c = *value;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') {
      output[cursor++] = c;
    } else if (cursor == 0 || output[cursor - 1] != '_') {
      output[cursor++] = '_';
    }
  }
  if (!cursor) {
    const char fallback[] = "bootstrap_failed";
    strncpy(output, fallback, N - 1);
    cursor = strlen(output);
  }
  output[cursor] = 0;
}

// Fixed binary persistence image.  It is deliberately not a wire format and
// is only used by board storage to stage credential metadata atomically.
struct ConfigurationImage {
  uint16_t version;
  DeviceConfiguration configuration;
  uint32_t checksum;
};

inline uint32_t configurationChecksum(const DeviceConfiguration& config) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&config);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < sizeof(config); ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

inline void makeConfigurationImage(const DeviceConfiguration& config,
                                   ConfigurationImage& image) {
  image.version = kConfigurationImageVersion;
  image.configuration = config;
  image.checksum = configurationChecksum(config);
}

inline bool verifyConfigurationImage(const ConfigurationImage& image) {
  return image.version == kConfigurationImageVersion &&
         configurationValid(image.configuration) &&
         image.checksum == configurationChecksum(image.configuration);
}

}  // namespace flova
