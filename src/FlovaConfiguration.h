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
static const size_t kTemplateIdBytes = 37;
static const size_t kChecksumTextBytes = 65;
static const size_t kProvisionTokenBytes = 65;
static const size_t kProvisioningErrorBytes = 48;
// Fits the built-in 32-byte SSID plus 64-byte password representation while
// keeping every persisted/runtime copy bounded on constrained boards.
static const uint16_t kConfigurationImageVersion = 3;

inline bool formatUuidText(const uint8_t (&bytes)[16], char* output,
                           size_t capacity) {
  static const char digits[] = "0123456789abcdef";
  if (!output || capacity < 37) return false;

  size_t cursor = 0;
  for (size_t i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) output[cursor++] = '-';
    output[cursor++] = digits[bytes[i] >> 4];
    output[cursor++] = digits[bytes[i] & 0x0f];
  }
  output[cursor] = 0;
  return true;
}

struct DeviceConfiguration {
  char deviceId[kDeviceIdBytes];
  char linkUrl[kLinkUrlBytes];
  char linkSecret[kSecretTextBytes];
  char templateVersionId[kTemplateIdBytes];
  char checksum[kChecksumTextBytes];
  uint32_t generation;
};

template <size_t N>
inline bool copyBounded(const char* value, char (&out)[N], bool required = false) {
  const size_t length = value ? strnlen(value, N) : 0;
  if (length >= N || (required && length == 0)) return false;
  if (length) memcpy(out, value, length);
  out[length] = 0;
  return true;
}

struct ProvisioningHandoff {
  char linkUrl[kLinkUrlBytes];
  char token[kProvisionTokenBytes];

  ProvisioningHandoff(const char* url = nullptr, const char* provisionToken = nullptr)
      : linkUrl(), token() {
    copyBounded(url, linkUrl);
    copyBounded(provisionToken, token);
  }
};

struct ProvisioningState {
  char linkUrl[kLinkUrlBytes];
  char token[kProvisionTokenBytes];
  char linkSecret[kSecretTextBytes];
};

struct ProvisioningHandoffImage {
  uint16_t version;
  ProvisioningState handoff;
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

inline bool configurationValid(const DeviceConfiguration& config) {
  return config.deviceId[0] &&
         strncmp(config.linkUrl, "wss://", 6) == 0 && config.linkSecret[0];
}

inline uint32_t provisioningChecksum(const ProvisioningState& handoff) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&handoff);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < sizeof(handoff); ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

inline bool provisioningValid(const ProvisioningState& handoff) {
  return handoff.token[0] && handoff.linkSecret[0] &&
         strncmp(handoff.linkUrl, "wss://", 6) == 0;
}

inline void makeProvisioningImage(const ProvisioningState& handoff,
                                   ProvisioningHandoffImage& image) {
  // The facade may parse directly into image.handoff to avoid another
  // maximum-sized provisioning object on a constrained board stack.
  if (&handoff != &image.handoff) image.handoff = handoff;
  image.version = kConfigurationImageVersion;
  image.attempts = 0;
  image.inProgress = 0;
  memset(image.lastError, 0, sizeof(image.lastError));
  image.checksum = provisioningChecksum(image.handoff);
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

inline bool terminalProvisioningError(const char* code) {
  return code &&
         (!strcmp(code, "invalid_provision_token") ||
          !strcmp(code, "provision_token_expired") ||
          !strcmp(code, "provision_token_attempts_exceeded") ||
          !strcmp(code, "provisioning_secret_mismatch"));
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
