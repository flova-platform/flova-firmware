#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// This header deliberately has no Arduino dependency. The CBOR/zcbor layer
// owns decoding into ConfigurationRecord; this installer owns transactional
// persistence and never reconstructs a complete configuration in RAM.

namespace flova {
namespace config {

// CONFIG_RECORD's complete Flova Link payload must fit in the 500 byte space
// after the 12-byte frame header. This leaves a conservative fixed maximum for
// its decoded record body and storage envelope. Board profiles may lower it.
#ifndef FLOVA_CONFIG_RECORD_BYTES
#define FLOVA_CONFIG_RECORD_BYTES 448
#endif

static const size_t kMaximumRecordBytes = FLOVA_CONFIG_RECORD_BYTES;
static const size_t kChecksumBytes = 32;
static_assert(kMaximumRecordBytes <= 448,
              "CONFIG_RECORD body limit must preserve the 512-byte complete frame limit");

enum class RecordKind : uint8_t {
  Datastream = 0,
  System = 1,
  Schedule = 2,
  Safety = 3,
  ScheduleOccurrences = 4
};

struct Checksum {
  uint8_t bytes[kChecksumBytes];

  Checksum() { memset(bytes, 0, sizeof(bytes)); }
  bool equals(const Checksum& other) const {
    return memcmp(bytes, other.bytes, sizeof(bytes)) == 0;
  }
};

struct Begin {
  uint64_t messageId;
  uint32_t generation;
  uint16_t schemaVersion;
  uint16_t maximumRecordBytes;
  uint32_t recordCount;
  Checksum checksum;

  Begin()
      : messageId(0), generation(0), schemaVersion(0), maximumRecordBytes(0),
        recordCount(0) {}
};

// The generated zcbor wrapper must reject a body longer than length before it
// copies into this record. The installer copies this one bounded value into its
// single workspace only while writing and reading it back.
struct Record {
  uint64_t messageId;
  uint32_t generation;
  uint32_t sequence;
  RecordKind kind;
  uint16_t length;
  uint8_t body[kMaximumRecordBytes];

  Record()
      : messageId(0), generation(0), sequence(0), kind(RecordKind::Datastream),
        length(0) {}
};

static_assert(sizeof(Record) <= 480,
              "configuration record workspace exceeded the constrained-device budget");

struct End {
  uint64_t messageId;
  uint32_t generation;
  uint32_t recordCount;
  Checksum checksum;

  End() : messageId(0), generation(0), recordCount(0) {}
};

// This is the durable A/B generation metadata. A storage implementation must
// write Finalized only after every record is durable, and promoteGeneration()
// must atomically switch the active-generation pointer. Therefore reset or
// power loss before promotion always leaves the old active generation intact.
struct GenerationManifest {
  uint32_t generation;
  uint16_t schemaVersion;
  uint16_t maximumRecordBytes;
  uint32_t recordCount;
  Checksum checksum;
  bool finalized;

  GenerationManifest()
      : generation(0), schemaVersion(0), maximumRecordBytes(0), recordCount(0),
        finalized(false) {}
};

// Storage must use two independently verifiable generations. beginInactive()
// selects and clears only the inactive generation; it must never erase the
// active generation. writeRecord() must durably replace exactly one sequence.
class ConfigurationStorage {
 public:
  virtual ~ConfigurationStorage() {}
  virtual bool activeGeneration(uint32_t& generation) const = 0;
  virtual bool generationManifest(uint32_t generation,
                                  GenerationManifest& manifest) const = 0;
  virtual bool pendingManifest(GenerationManifest& manifest) const = 0;
  virtual bool beginInactive(const GenerationManifest& manifest) = 0;
  virtual bool writeRecord(uint32_t generation, const Record& record) = 0;
  virtual bool readRecord(uint32_t generation, uint32_t sequence,
                          Record& record) const = 0;
  virtual bool finalizeInactive(const GenerationManifest& manifest) = 0;
  virtual bool promoteGeneration(uint32_t generation) = 0;
};

enum class Status : uint8_t {
  Accepted = 0,
  Duplicate = 1,
  AlreadyCommitted = 2,
  InvalidBegin = 3,
  InvalidRecord = 4,
  InvalidEnd = 5,
  TransferActive = 6,
  StaleGeneration = 7,
  OutOfOrder = 8,
  DuplicateMismatch = 9,
  ChecksumMismatch = 10,
  StorageFailure = 11,
  VerificationFailure = 12
};

struct Ack {
  uint64_t messageId;
  uint32_t generation;
  uint32_t sequence;
  Status status;

  Ack() : messageId(0), generation(0), sequence(0), status(Status::InvalidBegin) {}
  bool accepted() const {
    return status == Status::Accepted || status == Status::Duplicate ||
           status == Status::AlreadyCommitted;
  }
};

// SHA-256 is retained locally so CONFIG_END validates the CDDL sha256 checksum
// without allocating, retaining an input tree, or relying on a board crypto
// API. The defined hash input is the ordered persisted record stream:
// sequence:uint32-be || kind:uint8 || body-length:uint16-be || body bytes.
class Digest {
 public:
  Digest() { reset(); }

  void reset() {
    state_[0] = 0x6a09e667U;
    state_[1] = 0xbb67ae85U;
    state_[2] = 0x3c6ef372U;
    state_[3] = 0xa54ff53aU;
    state_[4] = 0x510e527fU;
    state_[5] = 0x9b05688cU;
    state_[6] = 0x1f83d9abU;
    state_[7] = 0x5be0cd19U;
    bitLength_ = 0;
    bufferLength_ = 0;
  }

  void addRecord(const Record& record) {
    const uint8_t envelope[7] = {
        static_cast<uint8_t>(record.sequence >> 24),
        static_cast<uint8_t>(record.sequence >> 16),
        static_cast<uint8_t>(record.sequence >> 8),
        static_cast<uint8_t>(record.sequence),
        static_cast<uint8_t>(record.kind),
        static_cast<uint8_t>(record.length >> 8),
        static_cast<uint8_t>(record.length)};
    update(envelope, sizeof(envelope));
    update(record.body, record.length);
  }

  void finish(Checksum& output) const {
    Digest copy(*this);
    copy.pad();
    for (size_t i = 0; i < 8; ++i) {
      output.bytes[i * 4] = static_cast<uint8_t>(copy.state_[i] >> 24);
      output.bytes[i * 4 + 1] = static_cast<uint8_t>(copy.state_[i] >> 16);
      output.bytes[i * 4 + 2] = static_cast<uint8_t>(copy.state_[i] >> 8);
      output.bytes[i * 4 + 3] = static_cast<uint8_t>(copy.state_[i]);
    }
  }

 private:
  static uint32_t rotateRight(uint32_t value, uint8_t bits) {
    return (value >> bits) | (value << (32 - bits));
  }

  static uint32_t choose(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
  }

  static uint32_t majority(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
  }

  static uint32_t bigEndian(const uint8_t* value) {
    return (static_cast<uint32_t>(value[0]) << 24) |
           (static_cast<uint32_t>(value[1]) << 16) |
           (static_cast<uint32_t>(value[2]) << 8) | value[3];
  }

  void update(const uint8_t* data, size_t length) {
    if (!data || !length) return;
    bitLength_ += static_cast<uint64_t>(length) * 8U;
    while (length) {
      const size_t available = sizeof(buffer_) - bufferLength_;
      const size_t copied = length < available ? length : available;
      memcpy(buffer_ + bufferLength_, data, copied);
      bufferLength_ += copied;
      data += copied;
      length -= copied;
      if (bufferLength_ == sizeof(buffer_)) {
        transform(buffer_);
        bufferLength_ = 0;
      }
    }
  }

  void pad() {
    const uint64_t length = bitLength_;
    buffer_[bufferLength_++] = 0x80;
    if (bufferLength_ > 56) {
      while (bufferLength_ < sizeof(buffer_)) buffer_[bufferLength_++] = 0;
      transform(buffer_);
      bufferLength_ = 0;
    }
    while (bufferLength_ < 56) buffer_[bufferLength_++] = 0;
    for (uint8_t i = 0; i < 8; ++i)
      buffer_[56 + i] = static_cast<uint8_t>(length >> (56 - i * 8));
    transform(buffer_);
    bufferLength_ = 0;
  }

  void transform(const uint8_t* block) {
    static const uint32_t constants[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
        0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
        0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
        0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
        0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
        0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
        0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
        0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
        0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
        0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
    uint32_t words[64];
    for (size_t i = 0; i < 16; ++i) words[i] = bigEndian(block + i * 4);
    for (size_t i = 16; i < 64; ++i) {
      const uint32_t a = rotateRight(words[i - 15], 7) ^
                         rotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
      const uint32_t b = rotateRight(words[i - 2], 17) ^
                         rotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
      words[i] = words[i - 16] + a + words[i - 7] + b;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (size_t i = 0; i < 64; ++i) {
      const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
      const uint32_t temporary1 = h + s1 + choose(e, f, g) + constants[i] + words[i];
      const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
      const uint32_t temporary2 = s0 + majority(a, b, c);
      h = g; g = f; f = e; e = d + temporary1;
      d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
  }

  uint32_t state_[8];
  uint64_t bitLength_;
  uint8_t buffer_[64];
  size_t bufferLength_;
};

class Installer {
 public:
  Installer(ConfigurationStorage& storage, uint32_t maximumRecords)
      : storage_(storage), maximumRecords_(maximumRecords), phase_(Phase::Idle),
        nextSequence_(0) {}

  Ack begin(const Begin& begin) {
    Ack ack = acknowledgement(begin.messageId, begin.generation, 0);
    if (!valid(begin)) return with(ack, Status::InvalidBegin);

    uint32_t active = 0;
    if (!storage_.activeGeneration(active)) return with(ack, Status::StorageFailure);
    if (begin.generation < active) return with(ack, Status::StaleGeneration);
    if (begin.generation == active) {
      GenerationManifest activeManifest;
      if (storage_.generationManifest(active, activeManifest) &&
          matches(activeManifest, begin) && activeManifest.finalized) {
        // A lost final bootstrap acknowledgement reconnects with the same
        // transaction. Keep the durable manifest available so its records
        // and CONFIG_END can be acknowledged idempotently as well.
        manifest_ = activeManifest;
        phase_ = Phase::Committed;
        nextSequence_ = manifest_.recordCount;
        return with(ack, Status::AlreadyCommitted);
      }
      return with(ack, Status::StaleGeneration);
    }

    if (phase_ == Phase::Receiving) {
      return with(ack, matches(manifest_, begin) ? Status::Duplicate : Status::TransferActive);
    }
    if (phase_ == Phase::Finalized) {
      return with(ack, matches(manifest_, begin) ? Status::Duplicate : Status::TransferActive);
    }
    if (phase_ == Phase::Committed)
      return with(ack, matches(manifest_, begin) ? Status::AlreadyCommitted : Status::StaleGeneration);

    GenerationManifest pending;
    if (storage_.pendingManifest(pending) && pending.generation > begin.generation)
      return with(ack, Status::StaleGeneration);
    if (storage_.pendingManifest(pending) && pending.generation == begin.generation &&
        pending.finalized && matches(pending, begin)) {
      manifest_ = pending;
      phase_ = Phase::Finalized;
      return with(ack, Status::Duplicate);
    }

    manifest_ = manifestFrom(begin);
    if (!storage_.beginInactive(manifest_)) return with(ack, Status::StorageFailure);
    digest_.reset();
    nextSequence_ = 0;
    phase_ = Phase::Receiving;
    return with(ack, Status::Accepted);
  }

  Ack record(const Record& record) {
    Ack ack = acknowledgement(record.messageId, record.generation, record.sequence);
    if (phase_ == Phase::Committed || phase_ == Phase::Finalized) {
      if (record.generation != manifest_.generation)
        return with(ack, record.generation < manifest_.generation ? Status::StaleGeneration
                                                                    : Status::InvalidRecord);
      if (!valid(record) || record.length > manifest_.maximumRecordBytes ||
          record.sequence >= manifest_.recordCount)
        return with(ack, Status::InvalidRecord);
      if (!storage_.readRecord(manifest_.generation, record.sequence, workspace_))
        return with(ack, Status::VerificationFailure);
      return with(ack, same(workspace_, record) ? Status::Duplicate : Status::DuplicateMismatch);
    }
    if (phase_ != Phase::Receiving) return with(ack, Status::InvalidRecord);
    if (record.generation != manifest_.generation)
      return with(ack, record.generation < manifest_.generation ? Status::StaleGeneration : Status::InvalidRecord);
    if (!valid(record) || record.length > manifest_.maximumRecordBytes ||
        record.sequence >= manifest_.recordCount)
      return with(ack, Status::InvalidRecord);

    if (record.sequence < nextSequence_) {
      if (!storage_.readRecord(manifest_.generation, record.sequence, workspace_))
        return with(ack, Status::VerificationFailure);
      return with(ack, same(workspace_, record) ? Status::Duplicate : Status::DuplicateMismatch);
    }
    if (record.sequence != nextSequence_) return with(ack, Status::OutOfOrder);

    // The only application record workspace is overwritten for every record.
    workspace_ = record;
    if (!storage_.writeRecord(manifest_.generation, workspace_))
      return with(ack, Status::StorageFailure);
    if (!storage_.readRecord(manifest_.generation, record.sequence, workspace_) ||
        !same(workspace_, record))
      return with(ack, Status::VerificationFailure);
    digest_.addRecord(workspace_);
    ++nextSequence_;
    return with(ack, Status::Accepted);
  }

  Ack end(const End& end) {
    Ack ack = acknowledgement(end.messageId, end.generation, end.recordCount);
    if (phase_ == Phase::Committed) {
      return with(ack, endMatches(end) ? Status::AlreadyCommitted : Status::InvalidEnd);
    }
    if (phase_ == Phase::Idle) {
      uint32_t active = 0;
      GenerationManifest manifest;
      if (!storage_.activeGeneration(active)) return with(ack, Status::StorageFailure);
      if (active == end.generation && storage_.generationManifest(active, manifest) &&
          manifest.finalized && endMatches(manifest, end))
        return with(ack, Status::AlreadyCommitted);
      return with(ack, Status::InvalidEnd);
    }
    if (end.generation != manifest_.generation || !endMatches(end))
      return with(ack, end.generation < manifest_.generation ? Status::StaleGeneration : Status::InvalidEnd);

    if (phase_ == Phase::Receiving) {
      if (nextSequence_ != manifest_.recordCount) return with(ack, Status::InvalidEnd);
      Checksum actual;
      digest_.finish(actual);
      if (!actual.equals(manifest_.checksum)) return with(ack, Status::ChecksumMismatch);
      if (!storage_.finalizeInactive(manifest_)) return with(ack, Status::StorageFailure);
      phase_ = Phase::Finalized;
    }

    // Finalization makes the complete inactive generation durable. Runtime
    // code must semantically validate every record before calling promote().
    return with(ack, Status::Accepted);
  }

  bool promote(uint32_t generation) {
    if (phase_ == Phase::Committed)
      return generation == manifest_.generation;
    if (phase_ != Phase::Finalized || generation != manifest_.generation)
      return false;
    if (!storage_.promoteGeneration(generation)) return false;
    phase_ = Phase::Committed;
    return true;
  }

  void reset() {
    // No persistent mutation: an interrupted transfer remains inactive and is
    // discarded only by a subsequent valid CONFIG_BEGIN for that generation.
    phase_ = Phase::Idle;
    nextSequence_ = 0;
    digest_.reset();
    workspace_ = Record();
  }

  size_t workspaceBytes() const { return sizeof(workspace_); }

  // Board restore code may reuse the installer's single bounded record
  // workspace instead of allocating a second 448-byte record buffer.
  Record& workspace() { return workspace_; }
  bool loadWorkspace(uint32_t generation, uint32_t sequence) {
    return storage_.readRecord(generation, sequence, workspace_);
  }

 private:
  enum class Phase : uint8_t { Idle, Receiving, Finalized, Committed };

  static Ack acknowledgement(uint64_t messageId, uint32_t generation, uint32_t sequence) {
    Ack ack;
    ack.messageId = messageId;
    ack.generation = generation;
    ack.sequence = sequence;
    return ack;
  }

  static Ack with(Ack ack, Status status) { ack.status = status; return ack; }

  static GenerationManifest manifestFrom(const Begin& begin) {
    GenerationManifest manifest;
    manifest.generation = begin.generation;
    manifest.schemaVersion = begin.schemaVersion;
    manifest.maximumRecordBytes = begin.maximumRecordBytes;
    manifest.recordCount = begin.recordCount;
    manifest.checksum = begin.checksum;
    return manifest;
  }

  bool valid(const Begin& begin) const {
    return begin.generation != 0 && begin.schemaVersion != 0 &&
           begin.recordCount <= maximumRecords_ && begin.maximumRecordBytes != 0 &&
           begin.maximumRecordBytes <= kMaximumRecordBytes;
  }

  static bool valid(const Record& record) {
    return record.length != 0 && record.length <= kMaximumRecordBytes &&
           static_cast<uint8_t>(record.kind) <= static_cast<uint8_t>(RecordKind::ScheduleOccurrences);
  }

  static bool matches(const GenerationManifest& manifest, const Begin& begin) {
    return manifest.generation == begin.generation &&
           manifest.schemaVersion == begin.schemaVersion &&
           manifest.maximumRecordBytes == begin.maximumRecordBytes &&
           manifest.recordCount == begin.recordCount && manifest.checksum.equals(begin.checksum);
  }

  bool endMatches(const End& end) const { return endMatches(manifest_, end); }

  static bool endMatches(const GenerationManifest& manifest, const End& end) {
    return manifest.generation == end.generation &&
           manifest.recordCount == end.recordCount && manifest.checksum.equals(end.checksum);
  }

  static bool same(const Record& left, const Record& right) {
    return left.generation == right.generation && left.sequence == right.sequence &&
           left.kind == right.kind && left.length == right.length &&
           memcmp(left.body, right.body, left.length) == 0;
  }

  ConfigurationStorage& storage_;
  uint32_t maximumRecords_;
  Phase phase_;
  uint32_t nextSequence_;
  GenerationManifest manifest_;
  Digest digest_;
  Record workspace_;
};

}  // namespace config
}  // namespace flova
