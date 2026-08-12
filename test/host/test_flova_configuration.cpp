#include "FlovaConfiguration.h"

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
  return 0;
}
