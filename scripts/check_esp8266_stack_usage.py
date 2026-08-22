#!/usr/bin/env python3
"""Fail when critical constrained-target stack frames regress."""

import re
from pathlib import Path


ESP8266_LIMITS = (
    ("setup", re.compile(r"\bvoid setup\(\)$"), 128),
    ("acceptProvisioning", re.compile(r"acceptProvisioning\("), 384),
    ("completeBootstrap", re.compile(r"completeBootstrap\("), 384),
    ("drainConfiguration", re.compile(r"drainConfiguration\("), 384),
    ("startConfigurationRestore", re.compile(r"startConfigurationRestore\("), 256),
    ("stepConfigurationWork", re.compile(r"stepConfigurationWork\(\)"), 256),
    ("stepConfigurationCandidate", re.compile(r"stepConfigurationCandidate\(\)"), 256),
    ("stepConfigurationDigest", re.compile(r"stepConfigurationDigest\(\)"), 256),
    ("stepConfigurationSemantic", re.compile(r"stepConfigurationSemantic\(\)"), 256),
    ("stepConfigurationReferences", re.compile(r"stepConfigurationReferences\(\)"), 256),
    ("stepConfigurationSchedules", re.compile(r"stepConfigurationSchedules\(\)"), 256),
    ("stepConfigurationApply", re.compile(r"stepConfigurationApply\(\)"), 256),
    ("validateConfigurationUnit", re.compile(r"validateConfigurationUnit\("), 256),
    ("finishConfigurationApply", re.compile(r"finishConfigurationApply\(\)"), 256),
    ("finishConfigurationVerification", re.compile(r"finishConfigurationVerification\(\)"), 256),
    ("processOta", re.compile(r"FlovaClient::processOta\(\)"), 512),
    ("FlovaEsp8266Platform::installOta", re.compile(r"FlovaEsp8266Platform::installOta\("), 768),
    ("sendDatastreamBinding", re.compile(r"sendDatastreamBinding\(\)"), 768),
    ("handleDatastreamBound", re.compile(r"handleDatastreamBound\("), 768),
    ("dispatchPendingFrame", re.compile(r"dispatchPendingFrame\(\)"), 256),
    ("ScheduleManifest::reset", re.compile(r"ScheduleManifest::reset\("), 512),
    ("ScheduleRuntime::clear", re.compile(r"ScheduleRuntime::clear\("), 512),
    ("ScheduleChunkCompiler::reset", re.compile(r"ScheduleChunkCompiler::reset\("), 512),
)

ESP32_LIMITS = (
    ("ScheduleManifest::reset", re.compile(r"ScheduleManifest::reset\("), 512),
    ("ScheduleRuntime::clear", re.compile(r"ScheduleRuntime::clear\("), 512),
    ("ScheduleChunkCompiler::reset", re.compile(r"ScheduleChunkCompiler::reset\("), 512),
)

OPTIONAL_LIMITS = {
    "sendDatastreamBinding",
    "ScheduleManifest::reset",
    "ScheduleRuntime::clear",
    "ScheduleChunkCompiler::reset",
}


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    failed = False
    for target, directory, limits in (
        ("ESP8266", root / ".pio" / "build" / "universal-esp8266", ESP8266_LIMITS),
        ("ESP32", root / ".pio" / "build" / "universal-esp32", ESP32_LIMITS),
    ):
        usages = {name: -1 for name, _, _ in limits}
        for path in directory.rglob("*.su"):
            for line in path.read_text(errors="replace").splitlines():
                parts = line.split("\t")
                if len(parts) < 3 or not parts[1].isdigit():
                    continue
                signature, size = parts[0], int(parts[1])
                for name, pattern, _ in limits:
                    if pattern.search(signature):
                        usages[name] = max(usages[name], size)

        for name, _, limit in limits:
            actual = usages[name]
            if actual < 0:
                if name in OPTIONAL_LIMITS:
                    print(f"{target} {name}: inlined (caller frame checked)")
                else:
                    print(f"missing {target} stack-usage record: {name}")
                    failed = True
            elif actual > limit:
                print(f"{target} stack frame too large: {name} uses {actual} bytes (limit {limit})")
                failed = True
            else:
                print(f"{target} {name}: {actual} / {limit} bytes")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
