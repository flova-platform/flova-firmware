# PlatformIO source anchor

This directory intentionally contains no firmware implementation. The
PlatformIO environments in `platformio.ini` select their application source
from `examples/*/src` with `build_src_filter`; the directory itself must exist
for PlatformIO to evaluate that filter.
