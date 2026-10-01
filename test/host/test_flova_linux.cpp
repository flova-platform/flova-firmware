#include <FlovaLinux.h>

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
  char directory[] = "/tmp/flova-linux-test-XXXXXX";
  assert(mkdtemp(directory));
  flova_linux::FileStorage storage(directory);
  assert(storage.begin());

  const char value[] = "state";
  assert(storage.write("record", value, sizeof(value)));
  char restored[sizeof(value)] = {};
  assert(storage.read("record", restored, sizeof(restored)));
  assert(memcmp(value, restored, sizeof(value)) == 0);
  assert(!storage.write("../escape", value, sizeof(value)));

  char source[sizeof(directory) + 16] = {};
  snprintf(source, sizeof(source), "%s/source", directory);
  int fd = open(source, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  assert(fd >= 0);
  const char payload[] = "hello\n";
  assert(write(fd, payload, sizeof(payload) - 1) ==
         static_cast<ssize_t>(sizeof(payload) - 1));
  close(fd);

  char otaRoot[sizeof(directory) + 16] = {};
  snprintf(otaRoot, sizeof(otaRoot), "%s/ota", directory);
  char active[sizeof(directory) + 24] = {};
  snprintf(active, sizeof(active), "%s/current", otaRoot);
  flova_linux::OtaUpdater updater(otaRoot, active);
  assert(updater.stage(source, "1.0.0", sizeof(payload) - 1,
                       "0000000000000000000000000000000000000000000000000000000000000000") ==
         flova_linux::OtaStageResult::HashMismatch);
  assert(updater.stage(source, "1.0.0", sizeof(payload) - 1,
                       "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03") ==
         flova_linux::OtaStageResult::Staged);
  assert(updater.activate("1.0.0"));
  assert(updater.confirm("1.0.0"));

  unlink(source);
  storage.clear();
  rmdir(directory);
  return 0;
}
