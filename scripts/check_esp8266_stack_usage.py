#!/usr/bin/env python3
"""Fail when critical Flova ESP8266 continuation-stack frames regress."""

import re
import sys
from pathlib import Path


LIMITS = (
    ("setup", re.compile(r"\bvoid setup\(\)$"), 128),
    ("restoreActiveConfiguration", re.compile(r"restoreActiveConfiguration\(\)"), 256),
    ("validateConfigurationGeneration", re.compile(r"validateConfigurationGeneration\("), 384),
    ("applyConfigurationGeneration", re.compile(r"applyConfigurationGeneration\("), 256),
    ("handleProvision", re.compile(r"handleProvision\(\)"), 768),
    ("publishStateBatch", re.compile(r"publishStateBatch"), 768),
    ("processPendingOta", re.compile(r"processPendingOta\(\)"), 768),
    ("ArduinoOtaInstaller::install", re.compile(r"ArduinoOtaInstaller::install\("), 768),
    ("ArduinoDeviceLink::dispatchPendingFrame", re.compile(r"dispatchPendingFrame\(\)"), 256),
)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    build = root / ".pio" / "build" / "universal-esp8266"
    usages: dict[str, int] = {name: -1 for name, _, _ in LIMITS}

    for path in build.rglob("*.su"):
        for line in path.read_text(errors="replace").splitlines():
            parts = line.split("\t")
            if len(parts) < 3 or not parts[1].isdigit():
                continue
            signature, size = parts[0], int(parts[1])
            for name, pattern, _ in LIMITS:
                if pattern.search(signature):
                    usages[name] = max(usages[name], size)

    missing = [name for name, _, _ in LIMITS if usages[name] < 0]
    exceeded = [
        (name, usages[name], limit)
        for name, _, limit in LIMITS
        if usages[name] > limit
    ]

    if missing:
        print(
            "missing ESP8266 stack-usage records: " + ", ".join(missing),
            file=sys.stderr,
        )
    for name, actual, limit in exceeded:
        print(
            f"ESP8266 stack frame too large: {name} uses {actual} bytes "
            f"(limit {limit})",
            file=sys.stderr,
        )
    if missing or exceeded:
        return 1

    for name, _, limit in LIMITS:
        print(f"{name}: {usages[name]} / {limit} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
