#!/usr/bin/env python3
"""Fail when critical ESP8266 continuation-stack frames regress."""

import re
from pathlib import Path


LIMITS = (
    ("setup", re.compile(r"\bvoid setup\(\)$"), 128),
    ("acceptProvisioning", re.compile(r"acceptProvisioning\("), 384),
    ("completeBootstrap", re.compile(r"completeBootstrap\("), 384),
    ("drainConfiguration", re.compile(r"drainConfiguration\("), 384),
    ("restoreActiveConfiguration", re.compile(r"restoreActiveConfiguration\(\)"), 256),
    ("processOta", re.compile(r"FlovaClient::processOta\(\)"), 512),
    ("ArduinoOtaInstaller::install", re.compile(r"ArduinoOtaInstaller::install\("), 768),
    ("sendDatastreamBinding", re.compile(r"sendDatastreamBinding\(\)"), 768),
    ("handleDatastreamBound", re.compile(r"handleDatastreamBound\("), 768),
    ("dispatchPendingFrame", re.compile(r"dispatchPendingFrame\(\)"), 256),
)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    build = root / ".pio" / "build" / "universal-esp8266"
    usages = {name: -1 for name, _, _ in LIMITS}

    for path in build.rglob("*.su"):
        for line in path.read_text(errors="replace").splitlines():
            parts = line.split("\t")
            if len(parts) < 3 or not parts[1].isdigit():
                continue
            signature, size = parts[0], int(parts[1])
            for name, pattern, _ in LIMITS:
                if pattern.search(signature):
                    usages[name] = max(usages[name], size)

    failed = False
    for name, _, limit in LIMITS:
        actual = usages[name]
        if actual < 0:
            print(f"missing ESP8266 stack-usage record: {name}")
            failed = True
        elif actual > limit:
            print(f"ESP8266 stack frame too large: {name} uses {actual} bytes (limit {limit})")
            failed = True
        else:
            print(f"{name}: {actual} / {limit} bytes")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
