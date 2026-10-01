#include <FlovaLinux.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <openssl/sha.h>

namespace {

uint64_t monotonicMs() {
  struct timespec value = {};
  clock_gettime(CLOCK_MONOTONIC, &value);
  return static_cast<uint64_t>(value.tv_sec) * 1000ULL +
         static_cast<uint64_t>(value.tv_nsec) / 1000000ULL;
}

bool safeComponent(const char* value) {
  if (!value || !value[0]) return false;
  for (const char* cursor = value; *cursor; ++cursor) {
    if (!(('a' <= *cursor && *cursor <= 'z') ||
          ('A' <= *cursor && *cursor <= 'Z') ||
          ('0' <= *cursor && *cursor <= '9') || *cursor == '_' ||
          *cursor == '-' || *cursor == '.'))
      return false;
  }
  return strcmp(value, ".") != 0 && strcmp(value, "..") != 0;
}

bool makeDirectory(const char* path) {
  if (!path || !path[0]) return false;
  char buffer[512] = {};
  if (strlen(path) >= sizeof(buffer)) return false;
  strcpy(buffer, path);
  for (char* cursor = buffer + 1; *cursor; ++cursor) {
    if (*cursor != '/') continue;
    *cursor = 0;
    if (::mkdir(buffer, 0750) != 0 && errno != EEXIST) return false;
    *cursor = '/';
  }
  return ::mkdir(buffer, 0750) == 0 || errno == EEXIST;
}

bool copyFile(const char* source, const char* destination, uint32_t expected,
              uint32_t* copied, char* digestHex, size_t digestCapacity) {
  int input = open(source, O_RDONLY | O_CLOEXEC);
  if (input < 0) return false;
  int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                    0640);
  if (output < 0) {
    close(input);
    return false;
  }
  uint8_t buffer[4096];
  uint32_t total = 0;
  SHA256_CTX digest;
  SHA256_Init(&digest);
  bool ok = true;
  for (;;) {
    const ssize_t count = read(input, buffer, sizeof(buffer));
    if (count == 0) break;
    if (count < 0 || total > UINT32_MAX - static_cast<uint32_t>(count) ||
        (expected && total + static_cast<uint32_t>(count) > expected) ||
        write(output, buffer, static_cast<size_t>(count)) != count) {
      ok = false;
      break;
    }
    SHA256_Update(&digest, buffer, static_cast<size_t>(count));
    total += static_cast<uint32_t>(count);
  }
  if (expected && total != expected) ok = false;
  if (copied) *copied = total;
  if (fsync(output) != 0) ok = false;
  close(output);
  close(input);
  if (ok && digestHex && digestCapacity >= 65) {
    uint8_t digestBytes[SHA256_DIGEST_LENGTH] = {};
    SHA256_Final(digestBytes, &digest);
    for (size_t i = 0; i < sizeof(digestBytes); ++i)
      snprintf(digestHex + i * 2, 3, "%02x", digestBytes[i]);
    digestHex[64] = 0;
  }
  return ok;
}

}  // namespace

namespace flova_linux {

uint64_t Clock::milliseconds() const { return monotonicMs(); }

bool Clock::utcValid() const { return utcBaseMs_ != 0; }

uint64_t Clock::utcMilliseconds() const {
  return utcValid() ? utcBaseMs_ + (monotonicMs() - monotonicBaseMs_) : 0;
}

void Clock::setUtc(uint64_t value, uint64_t) {
  utcBaseMs_ = value;
  monotonicBaseMs_ = monotonicMs();
}

FileStorage::FileStorage(const char* directory) : directory_{} {
  if (directory) {
    strncpy(directory_, directory, sizeof(directory_) - 1);
    directory_[sizeof(directory_) - 1] = 0;
  }
}

bool FileStorage::begin() { return directory_[0] && makeDirectory(directory_); }

bool FileStorage::pathFor(const char* key, char* output, size_t capacity) const {
  if (!safeComponent(key) || !output ||
      snprintf(output, capacity, "%s/%s", directory_, key) >=
          static_cast<int>(capacity))
    return false;
  return true;
}

bool FileStorage::read(const char* key, void* output, size_t size) {
  char path[sizeof(directory_) + 128] = {};
  if (!pathFor(key, path, sizeof(path)) || !output) return false;
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  const ssize_t count = ::read(fd, output, size);
  uint8_t extra = 0;
  const bool ok = count == static_cast<ssize_t>(size) &&
                 ::read(fd, &extra, sizeof(extra)) == 0;
  close(fd);
  return ok;
}

bool FileStorage::write(const char* key, const void* value, size_t size) {
  char path[sizeof(directory_) + 128] = {};
  char pending[sizeof(path) + 8] = {};
  if (!pathFor(key, path, sizeof(path)) || !value ||
      snprintf(pending, sizeof(pending), "%s.tmp", path) >=
          static_cast<int>(sizeof(pending)))
    return false;
  int fd = open(pending, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
  if (fd < 0) return false;
  const ssize_t written = ::write(fd, value, size);
  const bool ok = written == static_cast<ssize_t>(size) && fsync(fd) == 0;
  close(fd);
  if (!ok || rename(pending, path) != 0) {
    unlink(pending);
    return false;
  }
  return true;
}

bool FileStorage::remove(const char* key) {
  char path[sizeof(directory_) + 128] = {};
  if (!pathFor(key, path, sizeof(path))) return false;
  return unlink(path) == 0 || errno == ENOENT;
}

bool FileStorage::clear() {
  // Runtime keys are intentionally explicit and bounded. Unknown files remain
  // untouched so applications can share the directory safely.
  static const char* const keys[] = {"session", "session.pending", "config",
                                     "prov_pending", "ota_pending"};
  bool ok = true;
  for (const char* key : keys) ok = remove(key) && ok;
  return ok;
}

flova::StorageCapabilities FileStorage::capabilities() const {
  flova::StorageCapabilities value;
  value.usableBytes = 0;
  value.availableBytes = 0;
  value.maxRecordBytes = 16384;
  value.eraseBlockBytes = 1;
  value.writeGranularity = 1;
  value.persistent = true;
  value.wearSensitive = false;
  return value;
}

void Logger::log(const char* message) {
  if (message) fprintf(stderr, "%s\n", message);
}

OtaUpdater::OtaUpdater(const char* root, const char* activeLink)
    : root_{}, activeLink_{} {
  if (root) strncpy(root_, root, sizeof(root_) - 1);
  if (activeLink) strncpy(activeLink_, activeLink, sizeof(activeLink_) - 1);
}

bool OtaUpdater::pathFor(const char* suffix, const char* version, char* output,
                         size_t capacity) const {
  if (!safeComponent(version) || !output ||
      snprintf(output, capacity, "%s/%s-%s", root_, suffix, version) >=
          static_cast<int>(capacity))
    return false;
  return true;
}

OtaStageResult OtaUpdater::stage(const char* sourcePath, const char* version,
                                 uint32_t expectedBytes,
                                 const char* expectedSha256) {
  if (!sourcePath || !safeComponent(version) || !expectedBytes ||
      !expectedSha256 || strlen(expectedSha256) != 64)
    return OtaStageResult::InvalidOffer;
  if (!makeDirectory(root_)) return OtaStageResult::StorageFailure;
  char destination[sizeof(root_) + 128] = {};
  if (!pathFor("staged", version, destination, sizeof(destination)))
    return OtaStageResult::InvalidOffer;
  uint32_t copied = 0;
  char digest[65] = {};
  if (!copyFile(sourcePath, destination, expectedBytes, &copied, digest,
                sizeof(digest))) {
    unlink(destination);
    return copied != expectedBytes ? OtaStageResult::SizeMismatch
                                   : OtaStageResult::DownloadUnavailable;
  }
  if (strcasecmp(digest, expectedSha256) != 0) {
    unlink(destination);
    return OtaStageResult::HashMismatch;
  }
  return OtaStageResult::Staged;
}

bool OtaUpdater::activate(const char* version) {
  char staged[sizeof(root_) + 128] = {};
  if (!pathFor("staged", version, staged, sizeof(staged))) return false;
  return rename(staged, activeLink_) == 0;
}

bool OtaUpdater::confirm(const char* version) {
  char active[sizeof(root_) + 128] = {};
  if (!pathFor("active", version, active, sizeof(active))) return false;
  return rename(activeLink_, active) == 0;
}

bool OtaUpdater::rollback() {
  char previous[sizeof(root_) + 128] = {};
  if (snprintf(previous, sizeof(previous), "%s/previous", root_) >=
      static_cast<int>(sizeof(previous)))
    return false;
  return rename(previous, activeLink_) == 0;
}

}  // namespace flova_linux
