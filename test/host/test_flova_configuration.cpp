#include "FlovaConfiguration.h"
#include "FlovaLinkConfigurationStorage.h"
#include "FlovaStorageKey.h"
#include "adapters/ArduinoFlovaManualHardware.h"

#include <assert.h>
#include <string.h>

class NvsMappedStorage final : public flova::Storage {
 public:
  bool read(const char*, void*, size_t) override { return false; }
  bool write(const char* key, const void*, size_t) override {
    assert(writeCount < sizeof(writes) / sizeof(writes[0]));
    if (!flova::makeNvsStorageKey(key, writes[writeCount],
                                  sizeof(writes[writeCount])))
      return false;
    ++writeCount;
    return true;
  }
  bool remove(const char* key) override {
    char physical[16] = {};
    return flova::makeNvsStorageKey(key, physical, sizeof(physical));
  }

  char writes[8][16] = {};
  size_t writeCount = 0;
};

int main() {
  const uint8_t uuid[16] = {
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  char text[37] = {};

  assert(flova::formatUuidText(uuid, text, sizeof(text)));
  assert(strcmp(text, "00112233-4455-6677-8899-aabbccddeeff") == 0);
  assert(!flova::formatUuidText(uuid, text, sizeof(text) - 1));

  flova::ProvisioningHandoff handoff("wss://engine.example/api/device-link",
                                     "short-lived-token");
  assert(strcmp(handoff.token, "short-lived-token") == 0);

  flova::ProvisioningState state = {};
  assert(flova::copyBounded(handoff.linkUrl, state.linkUrl, true));
  assert(flova::copyBounded(handoff.token, state.token, true));
  assert(flova::copyBounded("generated-secret", state.linkSecret, true));
  flova::ProvisioningHandoffImage image = {};
  flova::makeProvisioningImage(state, image);
  assert(flova::verifyProvisioningImage(image));
  image.handoff.token[0] = 'x';
  assert(!flova::verifyProvisioningImage(image));

  assert(flova::provisioningBootMode(false, true, true) ==
         flova::ProvisioningBootMode::InterruptedBootstrap);
  assert(flova::provisioningBootMode(true, true, true) ==
         flova::ProvisioningBootMode::Runtime);
  assert(flova::terminalProvisioningError("invalid_provision_token"));
  assert(flova::terminalProvisioningError("provision_token_expired"));
  assert(flova::terminalProvisioningError(
      "provision_token_attempts_exceeded"));
  assert(flova::terminalProvisioningError("provisioning_secret_mismatch"));
  assert(!flova::terminalProvisioningError("bootstrap_timeout"));

  char storageKey[16] = {};
  assert(flova::makeNvsStorageKey("schedule.staging", storageKey,
                                  sizeof(storageKey)));
  assert(strcmp(storageKey, "ss") == 0);
  assert(flova::makeNvsStorageKey("history:63", storageKey,
                                  sizeof(storageKey)));
  assert(strcmp(storageKey, "h63") == 0);
  assert(flova::makeNvsStorageKey("dsid:65535", storageKey,
                                  sizeof(storageKey)));
  assert(strcmp(storageKey, "d65535") == 0);
  assert(flova::makeNvsStorageKey("flova_l_1_r_127", storageKey,
                                  sizeof(storageKey)));
  assert(strcmp(storageKey, "r1_127") == 0);
  assert(!flova::makeNvsStorageKey("history:256", storageKey,
                                   sizeof(storageKey)));
  assert(!flova::makeNvsStorageKey("unknown", storageKey,
                                   sizeof(storageKey)));

  ArduinoFlovaManualHardware manualHardware;
  const flova::HardwareCapabilities manualCapabilities =
      manualHardware.capabilities();
  assert(!manualCapabilities.automaticMapping);
  assert(manualCapabilities.inputSlots == 0);
  assert(manualCapabilities.outputSlots == 0);
  flova::config::Unit mappedUnit = {};
  mappedUnit.kind = flova::config::UnitKind::Datastream;
  mappedUnit.data.datastream.hasMapping = true;
  assert(manualHardware.validate(mappedUnit));
  assert(manualHardware.apply(mappedUnit));

  flova::config::Unit applicationUnit = {};
  applicationUnit.kind = flova::config::UnitKind::Datastream;
  assert(manualHardware.validate(applicationUnit));
  assert(manualHardware.apply(applicationUnit));

  NvsMappedStorage mapped;
  FlovaLinkConfigurationStorage configurationStorage(mapped, 4);
  flova::config::GenerationManifest manifest;
  manifest.generation = 1;
  manifest.schemaVersion = 1;
  manifest.maximumRecordBytes = flova::config::kMaximumRecordBytes;
  manifest.recordCount = 1;
  assert(configurationStorage.beginInactive(manifest));

  flova::config::Record record;
  record.generation = 1;
  record.sequence = 0;
  record.length = 1;
  record.body[0] = 0x42;
  assert(configurationStorage.writeRecord(1, record));
  assert(configurationStorage.finalizeInactive(manifest));
  assert(configurationStorage.promoteGeneration(1));
  assert(mapped.writeCount == 6);
  assert(strcmp(mapped.writes[0], "cp") == 0);
  assert(strcmp(mapped.writes[1], "m1") == 0);
  assert(strcmp(mapped.writes[2], "r1_000") == 0);
  assert(strcmp(mapped.writes[3], "cp") == 0);
  assert(strcmp(mapped.writes[4], "m1") == 0);
  assert(strcmp(mapped.writes[5], "cb") == 0);
  return 0;
}
