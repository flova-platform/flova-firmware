#pragma once
#include <Arduino.h>

struct FlovaOtaOffer {
  String installId;
  String releaseId;
  String version;
  String firmwareTarget;
  String artifactUrl;
  String sha256;
  String bootLayoutVersion;
  uint32_t sizeBytes = 0;
  uint16_t contractVersion = 0;
  bool allowDowngrade = false;
};

enum class FlovaOtaResult {
  Installed,
  DownloadFailed,
  HashMismatch,
  FlashFailed,
  ResourceUnavailable
};

class FlovaOtaInstaller {
 public:
  virtual ~FlovaOtaInstaller() {}
  virtual FlovaOtaResult install(const FlovaOtaOffer& offer) = 0;
};
