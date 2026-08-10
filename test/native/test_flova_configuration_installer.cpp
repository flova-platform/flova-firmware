#include <assert.h>
#include <string.h>

#include <FlovaConfigurationInstaller.h>

namespace {

using flova::config::Ack;
using flova::config::Begin;
using flova::config::Checksum;
using flova::config::ConfigurationStorage;
using flova::config::Digest;
using flova::config::End;
using flova::config::GenerationManifest;
using flova::config::Installer;
using flova::config::Record;
using flova::config::RecordKind;
using flova::config::Status;

static const size_t kTestRecords = 100;

struct Bank {
  GenerationManifest manifest;
  bool present;
  bool written[kTestRecords];
  Record records[kTestRecords];

  Bank() : present(false) { memset(written, 0, sizeof(written)); }
};

class TestConfigurationStorage : public ConfigurationStorage {
 public:
  TestConfigurationStorage() : active_(0), failPromotion_(false) {}

  bool activeGeneration(uint32_t& generation) const override {
    generation = active_;
    return true;
  }

  bool generationManifest(uint32_t generation, GenerationManifest& manifest) const override {
    for (size_t i = 0; i < 2; ++i) {
      if (banks_[i].present && banks_[i].manifest.generation == generation) {
        manifest = banks_[i].manifest;
        return true;
      }
    }
    return false;
  }

  bool pendingManifest(GenerationManifest& manifest) const override {
    for (size_t i = 0; i < 2; ++i) {
      if (banks_[i].present && banks_[i].manifest.generation != active_) {
        manifest = banks_[i].manifest;
        return true;
      }
    }
    return false;
  }

  bool beginInactive(const GenerationManifest& manifest) override {
    if (manifest.generation == active_ || manifest.recordCount > kTestRecords) return false;
    Bank& bank = banks_[activeBank() == 0 ? 1 : 0];
    bank = Bank();
    bank.present = true;
    bank.manifest = manifest;
    return true;
  }

  bool writeRecord(uint32_t generation, const Record& record) override {
    Bank* bank = find(generation);
    if (!bank || generation == active_ || record.sequence >= bank->manifest.recordCount)
      return false;
    bank->records[record.sequence] = record;
    bank->written[record.sequence] = true;
    return true;
  }

  bool readRecord(uint32_t generation, uint32_t sequence, Record& record) const override {
    const Bank* bank = find(generation);
    if (!bank || sequence >= bank->manifest.recordCount || !bank->written[sequence]) return false;
    record = bank->records[sequence];
    return true;
  }

  bool finalizeInactive(const GenerationManifest& manifest) override {
    Bank* bank = find(manifest.generation);
    if (!bank || manifest.generation == active_) return false;
    for (uint32_t i = 0; i < manifest.recordCount; ++i) if (!bank->written[i]) return false;
    bank->manifest = manifest;
    bank->manifest.finalized = true;
    return true;
  }

  bool promoteGeneration(uint32_t generation) override {
    Bank* bank = find(generation);
    if (!bank || !bank->manifest.finalized) return false;
    if (failPromotion_) return false;
    active_ = generation;
    return true;
  }

  void failPromotion(bool value) { failPromotion_ = value; }

 private:
  size_t activeBank() const {
    for (size_t i = 0; i < 2; ++i)
      if (banks_[i].present && banks_[i].manifest.generation == active_) return i;
    return 0;
  }

  Bank* find(uint32_t generation) {
    for (size_t i = 0; i < 2; ++i)
      if (banks_[i].present && banks_[i].manifest.generation == generation) return &banks_[i];
    return 0;
  }

  const Bank* find(uint32_t generation) const {
    for (size_t i = 0; i < 2; ++i)
      if (banks_[i].present && banks_[i].manifest.generation == generation) return &banks_[i];
    return 0;
  }

  Bank banks_[2];
  uint32_t active_;
  bool failPromotion_;
};

Record record(uint32_t generation, uint32_t sequence, uint8_t value) {
  Record out;
  out.messageId = 1000 + sequence;
  out.generation = generation;
  out.sequence = sequence;
  out.kind = sequence % 2 ? RecordKind::System : RecordKind::Datastream;
  out.length = 3;
  out.body[0] = value;
  out.body[1] = static_cast<uint8_t>(sequence);
  out.body[2] = static_cast<uint8_t>(value ^ sequence);
  return out;
}

Checksum checksum(uint32_t generation, uint32_t count) {
  Digest digest;
  for (uint32_t i = 0; i < count; ++i) digest.addRecord(record(generation, i, static_cast<uint8_t>(20 + i)));
  Checksum result;
  digest.finish(result);
  return result;
}

Begin begin(uint32_t generation, uint32_t count) {
  Begin out;
  out.messageId = 11;
  out.generation = generation;
  out.schemaVersion = 1;
  out.maximumRecordBytes = 16;
  out.recordCount = count;
  out.checksum = checksum(generation, count);
  return out;
}

End end(const Begin& begin) {
  End out;
  out.messageId = 99;
  out.generation = begin.generation;
  out.recordCount = begin.recordCount;
  out.checksum = begin.checksum;
  return out;
}

void appendAll(Installer& installer, uint32_t generation, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i)
    assert(installer.record(record(generation, i, static_cast<uint8_t>(20 + i))).status == Status::Accepted);
}

void testInterruptedTransferLeavesActiveUntouched() {
  TestConfigurationStorage storage;
  Installer installer(storage, kTestRecords);
  Begin transaction = begin(1, 3);
  assert(installer.begin(transaction).accepted());
  assert(installer.record(record(1, 0, 20)).accepted());
  installer.reset();
  uint32_t active = 99;
  assert(storage.activeGeneration(active) && active == 0);

  Installer restarted(storage, kTestRecords);
  assert(restarted.begin(transaction).status == Status::Accepted);
  appendAll(restarted, 1, 3);
  assert(restarted.end(end(transaction)).status == Status::Accepted);
  assert(storage.activeGeneration(active) && active == 1);
}

void testDuplicatesAndOrdering() {
  TestConfigurationStorage storage;
  Installer installer(storage, kTestRecords);
  Begin transaction = begin(1, 3);
  assert(installer.begin(transaction).accepted());
  Record first = record(1, 0, 20);
  assert(installer.record(first).status == Status::Accepted);
  assert(installer.record(first).status == Status::Duplicate);
  first.body[0]++;
  assert(installer.record(first).status == Status::DuplicateMismatch);
  assert(installer.record(record(1, 2, 22)).status == Status::OutOfOrder);
}

void testChecksumMismatchDoesNotPromote() {
  TestConfigurationStorage storage;
  Installer installer(storage, kTestRecords);
  Begin transaction = begin(1, 2);
  transaction.checksum.bytes[0] ^= 0xff;
  assert(installer.begin(transaction).accepted());
  appendAll(installer, 1, 2);
  End finish = end(transaction);
  assert(installer.end(finish).status == Status::ChecksumMismatch);
  uint32_t active = 99;
  assert(storage.activeGeneration(active) && active == 0);
}

void testStaleGenerationRejected() {
  TestConfigurationStorage storage;
  Installer installer(storage, kTestRecords);
  Begin first = begin(2, 1);
  assert(installer.begin(first).accepted());
  appendAll(installer, 2, 1);
  assert(installer.end(end(first)).accepted());
  Installer next(storage, kTestRecords);
  assert(next.begin(begin(1, 1)).status == Status::StaleGeneration);
  assert(next.begin(begin(2, 1)).status == Status::AlreadyCommitted);
}

void testCommittedGenerationReplayIsIdempotent() {
  TestConfigurationStorage storage;
  Installer first(storage, kTestRecords);
  Begin transaction = begin(1, 2);
  assert(first.begin(transaction).accepted());
  appendAll(first, 1, 2);
  assert(first.end(end(transaction)).status == Status::Accepted);

  Installer replay(storage, kTestRecords);
  assert(replay.begin(transaction).status == Status::AlreadyCommitted);
  assert(replay.record(record(1, 0, 20)).status == Status::Duplicate);
  assert(replay.record(record(1, 1, 21)).status == Status::Duplicate);
  assert(replay.end(end(transaction)).status == Status::AlreadyCommitted);

  Record changed = record(1, 0, 20);
  changed.body[0]++;
  assert(replay.record(changed).status == Status::DuplicateMismatch);
}

void testFinalCommitIsRetrySafe() {
  TestConfigurationStorage storage;
  Installer installer(storage, kTestRecords);
  Begin transaction = begin(1, 2);
  assert(installer.begin(transaction).accepted());
  appendAll(installer, 1, 2);
  storage.failPromotion(true);
  assert(installer.end(end(transaction)).status == Status::StorageFailure);
  uint32_t active = 99;
  assert(storage.activeGeneration(active) && active == 0);
  storage.failPromotion(false);
  assert(installer.end(end(transaction)).status == Status::Accepted);
  assert(storage.activeGeneration(active) && active == 1);

  // The first successful acknowledgement could have been lost. A fresh
  // installer must make the identical CONFIG_END harmless and explicit.
  Installer afterReconnect(storage, kTestRecords);
  assert(afterReconnect.end(end(transaction)).status == Status::AlreadyCommitted);
}

void testWorkspaceDoesNotDependOnConfigurationCount() {
  static_assert(sizeof(Installer) < 1024, "configuration installer workspace must remain bounded");
  TestConfigurationStorage storage;
  Installer five(storage, 5);
  Installer hundred(storage, 100);
  assert(sizeof(five) == sizeof(hundred));
  assert(five.workspaceBytes() == sizeof(Record));
  assert(hundred.workspaceBytes() == sizeof(Record));
}

void testDigestIsSha256OfThePersistedRecordStream() {
  const uint8_t expected[] = {
      0x15, 0x5f, 0xe2, 0x72, 0x88, 0xd4, 0xc8, 0xd1,
      0x87, 0x74, 0x68, 0x7e, 0xfe, 0xe5, 0x71, 0xeb,
      0x73, 0x80, 0xcd, 0x35, 0x54, 0x78, 0xcc, 0xe7,
      0x8e, 0x20, 0xaf, 0x82, 0xee, 0xe4, 0xe5, 0xa7};
  Digest digest;
  digest.addRecord(record(1, 0, 20));
  Checksum actual;
  digest.finish(actual);
  assert(memcmp(actual.bytes, expected, sizeof(expected)) == 0);
}

}  // namespace

int main() {
  testInterruptedTransferLeavesActiveUntouched();
  testDuplicatesAndOrdering();
  testChecksumMismatchDoesNotPromote();
  testStaleGenerationRejected();
  testCommittedGenerationReplayIsIdempotent();
  testFinalCommitIsRetrySafe();
  testWorkspaceDoesNotDependOnConfigurationCount();
  testDigestIsSha256OfThePersistedRecordStream();
  return 0;
}
