#include "FlovaConfiguration.h"
#include "FlovaStorageKey.h"

#include <assert.h>
#include <string.h>

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
  return 0;
}
