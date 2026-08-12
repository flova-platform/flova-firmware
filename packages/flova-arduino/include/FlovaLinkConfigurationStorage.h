#pragma once

#include <stdio.h>
#include <string.h>

#include <FlovaCore.h>
#include <FlovaConfigurationInstaller.h>

// Shared fixed-key A/B adapter. Board storage supplies atomic bounded binary
// reads/writes; this class owns generation metadata and record-slot naming.
// It never retains more than one persisted record in a local workspace.
class FlovaLinkConfigurationStorage : public flova::config::ConfigurationStorage {
 public:
  FlovaLinkConfigurationStorage(flova::Storage& storage, uint32_t maximumRecords)
      : storage_(storage), maximumRecords_(maximumRecords) {}

  bool activeGeneration(uint32_t& generation) const override {
    uint32_t previous = 0;
    generations(generation, previous);
    return true;
  }

  // Returns the two independently checksummed pointers in descending order.
  // Runtime restore can validate the newest bank and fall back to the previous
  // complete bank without guessing from parity.
  void generations(uint32_t& newest, uint32_t& previous) const {
    Pointer left = {}, right = {};
    const bool leftOk = loadPointer("flova_l_a", left);
    const bool rightOk = loadPointer("flova_l_b", right);
    newest = previous = 0;
    if (leftOk) newest = left.generation;
    if (rightOk) {
      if (right.generation > newest) {
        previous = newest;
        newest = right.generation;
      } else if (right.generation != newest) {
        previous = right.generation;
      }
    }
  }

  bool discardNewestGeneration() {
    uint32_t newest = 0;
    uint32_t previous = 0;
    generations(newest, previous);
    return discardGeneration(newest);
  }

  bool discardGeneration(uint32_t generation) {
    if (!generation) return false;
    bool success = true;
    Pointer pointer = {};
    if (loadPointer("flova_l_a", pointer) && pointer.generation == generation) {
      success = storage_.remove("flova_l_a") && success;
    }
    if (loadPointer("flova_l_b", pointer) && pointer.generation == generation) {
      success = storage_.remove("flova_l_b") && success;
    }
    StoredManifest pending = {};
    if (load("flova_l_p", pending) && pending.generation == generation)
      success = storage_.remove("flova_l_p") && success;
    success = storage_.remove(manifestKey(generation)) && success;
    return success;
  }

  bool generationManifest(uint32_t generation,
                          flova::config::GenerationManifest& manifest) const override {
    if (!generation) return false;
    StoredManifest stored = {};
    return load(manifestKey(generation), stored) && unpack(stored, manifest);
  }

  bool pendingManifest(flova::config::GenerationManifest& manifest) const override {
    StoredManifest stored = {};
    return load("flova_l_p", stored) && unpack(stored, manifest);
  }

  bool beginInactive(const flova::config::GenerationManifest& manifest) override {
    if (!manifest.generation || manifest.recordCount > maximumRecords_ ||
        manifest.maximumRecordBytes > flova::config::kMaximumRecordBytes)
      return false;
    StoredManifest stored = {};
    pack(manifest, stored);
    return save("flova_l_p", stored) && save(manifestKey(manifest.generation), stored);
  }

  bool writeRecord(uint32_t generation, const flova::config::Record& record) override {
    if (!generation || record.sequence >= maximumRecords_ ||
        record.length > flova::config::kMaximumRecordBytes)
      return false;
    memset(&storedWorkspace_, 0, sizeof(storedWorkspace_));
    StoredRecord& stored = storedWorkspace_;
    stored.magic = kRecordMagic;
    stored.generation = record.generation;
    stored.sequence = record.sequence;
    stored.kind = static_cast<uint8_t>(record.kind);
    stored.length = record.length;
    memcpy(stored.body, record.body, record.length);
    stored.checksum = checksum(reinterpret_cast<const uint8_t*>(&stored),
                                offsetof(StoredRecord, checksum));
    return save(recordKey(generation, record.sequence), stored);
  }

  bool readRecord(uint32_t generation, uint32_t sequence,
                  flova::config::Record& record) const override {
    memset(&storedWorkspace_, 0, sizeof(storedWorkspace_));
    StoredRecord& stored = storedWorkspace_;
    if (!load(recordKey(generation, sequence), stored) || stored.magic != kRecordMagic ||
        stored.generation != generation || stored.sequence != sequence ||
        stored.length > flova::config::kMaximumRecordBytes ||
        stored.checksum != checksum(reinterpret_cast<const uint8_t*>(&stored),
                                    offsetof(StoredRecord, checksum)))
      return false;
    record.generation = stored.generation;
    record.sequence = stored.sequence;
    record.kind = static_cast<flova::config::RecordKind>(stored.kind);
    record.length = stored.length;
    memcpy(record.body, stored.body, stored.length);
    return true;
  }

  bool finalizeInactive(const flova::config::GenerationManifest& manifest) override {
    StoredManifest stored = {};
    pack(manifest, stored);
    stored.finalized = 1;
    stored.checksum = checksum(reinterpret_cast<const uint8_t*>(&stored),
                               offsetof(StoredManifest, checksum));
    return save("flova_l_p", stored) && save(manifestKey(manifest.generation), stored);
  }

  bool promoteGeneration(uint32_t generation) override {
    uint32_t active = 0;
    if (!activeGeneration(active) || generation < active) return false;
    Pointer pointer = {};
    pointer.magic = kPointerMagic;
    pointer.generation = generation;
    pointer.checksum = checksum(reinterpret_cast<const uint8_t*>(&pointer),
                                offsetof(Pointer, checksum));
    const char* key = active & 1U ? "flova_l_a" : "flova_l_b";
    return save(key, pointer);
  }

 private:
  static const uint32_t kManifestMagic = 0x46434D31UL;
  static const uint32_t kRecordMagic = 0x46435231UL;
  static const uint32_t kPointerMagic = 0x46435031UL;

  struct StoredManifest {
    uint32_t magic;
    uint32_t generation;
    uint16_t schemaVersion;
    uint16_t maximumRecordBytes;
    uint32_t recordCount;
    uint8_t sha256[32];
    uint8_t finalized;
    uint8_t reserved[3];
    uint32_t checksum;
  };

  struct StoredRecord {
    uint32_t magic;
    uint32_t generation;
    uint32_t sequence;
    uint8_t kind;
    uint8_t reserved;
    uint16_t length;
    uint8_t body[flova::config::kMaximumRecordBytes];
    uint32_t checksum;
  };

  struct Pointer {
    uint32_t magic;
    uint32_t generation;
    uint32_t checksum;
  };

  static uint32_t checksum(const uint8_t* data, size_t length) {
    uint32_t value = 2166136261UL;
    for (size_t i = 0; i < length; ++i) {
      value ^= data[i];
      value *= 16777619UL;
    }
    return value;
  }

  void pack(const flova::config::GenerationManifest& input,
            StoredManifest& output) const {
    memset(&output, 0, sizeof(output));
    output.magic = kManifestMagic;
    output.generation = input.generation;
    output.schemaVersion = input.schemaVersion;
    output.maximumRecordBytes = input.maximumRecordBytes;
    output.recordCount = input.recordCount;
    memcpy(output.sha256, input.checksum.bytes, sizeof(output.sha256));
    output.finalized = input.finalized ? 1 : 0;
    output.checksum = checksum(reinterpret_cast<const uint8_t*>(&output),
                               offsetof(StoredManifest, checksum));
  }

  static bool unpack(const StoredManifest& input,
                     flova::config::GenerationManifest& output) {
    if (input.magic != kManifestMagic ||
        input.checksum != checksum(reinterpret_cast<const uint8_t*>(&input),
                                   offsetof(StoredManifest, checksum)))
      return false;
    output.generation = input.generation;
    output.schemaVersion = input.schemaVersion;
    output.maximumRecordBytes = input.maximumRecordBytes;
    output.recordCount = input.recordCount;
    memcpy(output.checksum.bytes, input.sha256, sizeof(output.checksum.bytes));
    output.finalized = input.finalized != 0;
    return true;
  }

  const char* manifestKey(uint32_t generation) const {
    manifestKey_[0] = 'f';
    manifestKey_[1] = 'l';
    manifestKey_[2] = 'o';
    manifestKey_[3] = 'v';
    manifestKey_[4] = 'a';
    manifestKey_[5] = '_';
    manifestKey_[6] = 'l';
    manifestKey_[7] = (generation & 1U) ? '1' : '0';
    manifestKey_[8] = '_';
    manifestKey_[9] = 'm';
    manifestKey_[10] = 0;
    return manifestKey_;
  }

  const char* recordKey(uint32_t generation, uint32_t sequence) const {
    snprintf(recordKey_, sizeof(recordKey_), "flova_l_%u_r_%03lu",
             static_cast<unsigned>(generation & 1U),
             static_cast<unsigned long>(sequence));
    return recordKey_;
  }

  template <typename T>
  bool load(const char* key, T& output) const {
    return storage_.read(key, &output, sizeof(output));
  }

  template <typename T>
  bool save(const char* key, const T& value) {
    return storage_.write(key, &value, sizeof(value));
  }

  bool loadPointer(const char* key, Pointer& pointer) const {
    return load(key, pointer) && pointer.magic == kPointerMagic &&
           pointer.checksum == checksum(reinterpret_cast<const uint8_t*>(&pointer),
                                        offsetof(Pointer, checksum));
  }

  flova::Storage& storage_;
  uint32_t maximumRecords_;
  mutable StoredRecord storedWorkspace_ = {};
  mutable char manifestKey_[12] = {};
  mutable char recordKey_[16] = {};
};
