#pragma once

#include <stdint.h>
#include <stddef.h>

#include <FlovaDevice.h>

namespace flova_linux {

class Clock final : public flova::Clock {
 public:
  uint64_t milliseconds() const override;
  bool utcValid() const override;
  uint64_t utcMilliseconds() const override;
  void setUtc(uint64_t value, uint64_t uncertaintyMs) override;

 private:
  uint64_t utcBaseMs_ = 0;
  uint64_t monotonicBaseMs_ = 0;
};

class FileStorage final : public flova::Storage {
 public:
  explicit FileStorage(const char* directory);

  bool begin() override;
  bool read(const char* key, void* output, size_t size) override;
  bool write(const char* key, const void* value, size_t size) override;
  bool remove(const char* key) override;
  bool clear() override;
  flova::StorageCapabilities capabilities() const override;

 private:
  bool pathFor(const char* key, char* output, size_t capacity) const;
  char directory_[256];
};

class Logger final : public flova::Logger {
 public:
  void log(const char* message) override;
};

enum class OtaStageResult : uint8_t {
  Staged,
  InvalidOffer,
  DownloadUnavailable,
  SizeMismatch,
  HashUnavailable,
  HashMismatch,
  StorageFailure
};

// Linux never replaces its own executable. The supervisor switches the active
// version after stage() succeeds and calls confirm() after health checks.
class OtaUpdater {
 public:
  OtaUpdater(const char* root, const char* activeLink);

  OtaStageResult stage(const char* sourcePath, const char* version,
                       uint32_t expectedBytes, const char* expectedSha256);
  bool activate(const char* version);
  bool confirm(const char* version);
  bool rollback();

 private:
  bool pathFor(const char* suffix, const char* version, char* output,
               size_t capacity) const;
  char root_[256];
  char activeLink_[256];
};

}  // namespace flova_linux
