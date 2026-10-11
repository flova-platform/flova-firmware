# Flova Linux

`flova-linux` is the native POSIX support package for Raspberry Pi and other
Linux hosts. It installs as a CMake package and exposes the same typed
datastream helpers used by the Arduino facades.

```cpp
#include <FlovaLinux.h>

FlovaLinux device;
auto relay = FLOVA_DATASTREAM(device, bool, "relay");

int main() {
  FLOVA_ON_WRITE(relay, [](bool value) { /* apply output */ });
  if (!device.begin()) return 1;
  for (;;) device.run();
}
```

`run()` performs one bounded nonblocking step. Applications can call it from
their own poll or epoll loop.

```cmake
find_package(FlovaLinux CONFIG REQUIRED)
target_link_libraries(my_device PRIVATE Flova::Linux)
```

The package provides `FlovaLinux`, POSIX WSS transport, `Clock`, `FileStorage`,
`Logger`, and `OtaUpdater`. The transport uses fixed receive and transmit
workspaces, OpenSSL certificate verification, and reconnect backoff.
`OtaUpdater` verifies the declared size and SHA-256, stages the artifact, and
exposes activation/confirmation/rollback operations for an external systemd
or equivalent supervisor.

Direct Ethernet devices use the host's normal Linux network interface. The
application owns its event loop and hardware integration while `FlovaLinux`
owns the WSS connection and device protocol.
