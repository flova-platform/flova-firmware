#include <FlovaLinux.h>

#include <unistd.h>

namespace {

void applyRelay(bool enabled) {
  // Replace this with the application's GPIO, serial, or process adapter.
  fprintf(stdout, "relay=%s\n", enabled ? "on" : "off");
}

}  // namespace

int main() {
  FlovaLinux device;
  auto relay = FLOVA_DATASTREAM(device, bool, "relay");
  FLOVA_ON_WRITE(relay, applyRelay);

  if (!device.begin()) {
    fprintf(stderr, "Flova startup failed: %s\n", device.lastError());
    return 1;
  }

  while (device.running()) {
    device.run();
    // The SDK performs bounded, nonblocking work. Applications may replace
    // this short yield with their own poll/epoll event loop.
    usleep(1000);
  }
  return 0;
}
