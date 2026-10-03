# Flova Linux

`flova-linux` is the native POSIX support package for Raspberry Pi and other
Linux hosts. It installs as a CMake package and keeps the portable
`flova::Device` API unchanged.

```cmake
find_package(FlovaLinux CONFIG REQUIRED)
target_link_libraries(my_device PRIVATE Flova::Linux)
```

The package provides `flova_linux::Clock`, `FileStorage`, `Logger`, and
`OtaUpdater`. Applications provide the `flova::Link` implementation for their
network or gateway transport. `OtaUpdater` verifies the declared size and
SHA-256, stages the artifact, and exposes activation/confirmation/rollback
operations for an external systemd or equivalent supervisor.

Direct Ethernet devices use the host's normal Linux network interface. Include
`FlovaLinuxEthernet.h` when an application wants the bounded socket seam; the
application supplies the WSS `flova::Link`, while Linux owns Ethernet, DHCP,
routing, and certificate storage.
