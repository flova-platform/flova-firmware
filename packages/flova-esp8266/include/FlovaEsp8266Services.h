#pragma once

#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiUdp.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <FlovaProvisioningAdapter.h>
#include <adapters/ArduinoFlovaUtcBootstrap.h>

class FlovaEsp8266Storage : public flova::Storage {
 public:
  bool begin() override {
    if (mounted_) return true;
    clearError();
    if (!LittleFS.begin()) {
      setError("mount");
      return false;
    }
    if (LittleFS.exists("/flova")) {
      File directory = LittleFS.open("/flova", "r");
      const bool validDirectory = directory && directory.isDirectory();
      if (directory) directory.close();
      if (!validDirectory) {
        LittleFS.end();
        setError("directory");
        return false;
      }
    } else if (!LittleFS.mkdir("/flova")) {
      LittleFS.end();
      setError("directory");
      return false;
    }
    mounted_ = true;
    refreshCapacity();
    Serial.printf("[flova] storage ready total=%lu available=%lu\n",
                  static_cast<unsigned long>(totalBytes_),
                  static_cast<unsigned long>(availableBytes_));
    return true;
  }

  bool read(const char* key, void* output, size_t size) override {
    char path[56] = {}, backup[64] = {};
    if (!mounted_) {
      setError("not_ready");
      return false;
    }
    if (!makePaths(key, path, sizeof(path), backup, sizeof(backup)) ||
        !output || !size || size > kMaximumRecordBytes)
      return false;
    File file = openRecord(path, backup, size);
    if (!file) return false;
    const size_t readBytes = file.read(reinterpret_cast<uint8_t*>(output), size);
    file.close();
    return readBytes == size;
  }

  bool write(const char* key, const void* value, size_t size) override {
    char path[56] = {}, next[64] = {}, backup[64] = {};
    if (!mounted_) {
      setError("not_ready");
      return false;
    }
    if (!makePath(key, path, sizeof(path)) ||
        snprintf(next, sizeof(next), "%s.next", path) >= static_cast<int>(sizeof(next)) ||
        snprintf(backup, sizeof(backup), "%s.backup", path) >= static_cast<int>(sizeof(backup)) ||
        !value || !size || size > kMaximumRecordBytes) {
      setError("arguments");
      return false;
    }

    // A reset can leave the staging name behind. Remove it before opening the
    // next record so a retry always starts from a known transaction state.
    if (LittleFS.exists(next) && !LittleFS.remove(next)) {
      setError("remove_next");
      return false;
    }
    File file = LittleFS.open(next, "w");
    if (!file) {
      setError("open_next");
      return false;
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value);
    size_t writtenBytes = 0;
    while (writtenBytes < size) {
      const size_t chunk = size - writtenBytes < 64 ? size - writtenBytes : 64;
      if (file.write(bytes + writtenBytes, chunk) != chunk) {
        file.close();
        LittleFS.remove(next);
        setError("write_next");
        return false;
      }
      writtenBytes += chunk;
      yield();
    }
    file.close();

    // Verify the staged bytes before moving either name. This catches a
    // partial flash write while the previous primary record is untouched.
    if (!matches(next, value, size)) {
      LittleFS.remove(next);
      setError("verify_next");
      return false;
    }

    if (LittleFS.exists(path)) {
      if (LittleFS.exists(backup) && !LittleFS.remove(backup)) {
        LittleFS.remove(next);
        setError("remove_backup");
        return false;
      }
      if (!LittleFS.rename(path, backup)) {
        LittleFS.remove(next);
        setError("rename_backup");
        return false;
      }
    }
    if (!LittleFS.rename(next, path)) {
      if (!LittleFS.exists(path) && LittleFS.exists(backup))
        LittleFS.rename(backup, path);
      setError("rename_primary");
      return false;
    }
    if (!matches(path, value, size)) {
      LittleFS.remove(path);
      if (LittleFS.exists(backup)) LittleFS.rename(backup, path);
      setError("verify_primary");
      return false;
    }
    LittleFS.remove(backup);
    refreshCapacity();
    clearError();
    return true;
  }

  bool remove(const char* key) override {
    char path[56] = {}, next[64] = {}, backup[64] = {};
    if (!mounted_) {
      setError("not_ready");
      return false;
    }
    if (!makePath(key, path, sizeof(path)) ||
        snprintf(next, sizeof(next), "%s.next", path) >=
            static_cast<int>(sizeof(next)) ||
        snprintf(backup, sizeof(backup), "%s.backup", path) >=
            static_cast<int>(sizeof(backup))) {
      setError("arguments");
      return false;
    }
    const bool primaryRemoved = !LittleFS.exists(path) || LittleFS.remove(path);
    const bool nextRemoved = !LittleFS.exists(next) || LittleFS.remove(next);
    const bool backupRemoved =
        !LittleFS.exists(backup) || LittleFS.remove(backup);
    if (!primaryRemoved || !nextRemoved || !backupRemoved) {
      setError("remove");
      return false;
    }
    refreshCapacity();
    clearError();
    return true;
  }

  // Safe for diagnostics: this is an internal operation stage, never a path,
  // credential, token, or payload.
  const char* lastError() const { return lastError_; }

  flova::StorageCapabilities capabilities() const override {
    flova::StorageCapabilities value;
    value.usableBytes = totalBytes_;
    value.availableBytes = availableBytes_;
    value.maxRecordBytes = kMaximumRecordBytes;
    value.eraseBlockBytes = 8192;
    value.writeGranularity = 1;
    value.persistent = true;
    value.wearSensitive = true;
    return value;
  }

 private:
  static const size_t kMaximumKeyBytes = 21;
  static const size_t kMaximumRecordBytes = 2048;

  void refreshCapacity() {
    FSInfo info = {};
    if (!mounted_ || !LittleFS.info(info)) return;
    totalBytes_ = info.totalBytes;
    availableBytes_ = info.usedBytes < info.totalBytes
                          ? info.totalBytes - info.usedBytes
                          : 0;
  }

  static bool makePaths(const char* key, char* path, size_t pathCapacity,
                        char* backup, size_t backupCapacity) {
    if (!makePath(key, path, pathCapacity)) return false;
    const int written = snprintf(backup, backupCapacity, "%s.backup", path);
    return written > 0 && static_cast<size_t>(written) < backupCapacity;
  }

  static File openRecord(const char* path, const char* backup, size_t size) {
    File file = LittleFS.open(path, "r");
    if (file && static_cast<size_t>(file.size()) == size) return file;
    if (file) file.close();
    file = LittleFS.open(backup, "r");
    if (file && static_cast<size_t>(file.size()) == size) return file;
    if (file) file.close();
    return File();
  }

  void setError(const char* value) { lastError_ = value ? value : "unknown"; }
  void clearError() { lastError_ = "none"; }

  static bool matches(const char* path, const void* value, size_t size) {
    File file = LittleFS.open(path, "r");
    if (!file || static_cast<size_t>(file.size()) != size) {
      if (file) file.close();
      return false;
    }
    const uint8_t* expected = static_cast<const uint8_t*>(value);
    uint8_t chunk[32] = {};
    size_t offset = 0;
    while (offset < size) {
      const size_t count = size - offset < sizeof(chunk)
                               ? size - offset
                               : sizeof(chunk);
      if (file.read(chunk, count) != static_cast<int>(count) ||
          memcmp(chunk, expected + offset, count) != 0) {
        file.close();
        return false;
      }
      offset += count;
    }
    file.close();
    return true;
  }

  static bool makePath(const char* key, char* output, size_t capacity) {
    if (!key || !key[0] || !output) return false;
    size_t length = 0;
    for (const char* cursor = key; *cursor; ++cursor, ++length) {
      if (length >= kMaximumKeyBytes) return false;
      const char value = *cursor;
      if (!((value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '_' || value == '-' ||
            value == '.' || value == ':'))
        return false;
    }
    if ((strcmp(key, ".") == 0) || (strcmp(key, "..") == 0)) return false;
    const int written = snprintf(output, capacity, "/flova/%s.bin", key);
    return written > 0 && static_cast<size_t>(written) < capacity;
  }

  bool mounted_ = false;
  uint32_t totalBytes_ = 0;
  uint32_t availableBytes_ = 0;
  const char* lastError_ = "none";
};

class FlovaEsp8266Runtime : public FlovaProvisioningAdapter {
 public:
  void loop() override { utc_.run(runtimeConnected()); }
  bool runtimeConnected() const override { return WiFi.status() == WL_CONNECTED; }
  bool clockReady() const override { return utc_.ready(); }

  bool defaultHardwareId(char* output, size_t capacity) const override {
    return output && capacity >= 24 &&
           snprintf(output, capacity, "esp8266-%06lx",
                    static_cast<unsigned long>(ESP.getChipId())) > 0;
  }

  const char* defaultFirmwareTarget() const override {
    return "custom_arduino_esp8266";
  }

 private:
  ArduinoFlovaUtcBootstrap<WiFiUDP> utc_;
};
