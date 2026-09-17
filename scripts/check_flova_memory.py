#!/usr/bin/env python3
"""Report linked RAM/IRAM and enforce reviewed image budgets (no device access)."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('environments', nargs='+')
    parser.add_argument('--stock-dram', action='store_true',
                        help='also enforce the ESP8266 no-IRAM TLS lower bound')
    args = parser.parse_args()
    budgets = json.loads((ROOT / 'scripts/flova_memory_budgets.json').read_text())
    failed = False
    for environment in args.environments:
        profile = budgets['profiles'].get(environment)
        if profile is None:
            sys.exit(f'No reviewed budget for {environment}')
        esp8266 = profile['board'] == 'esp8266'
        tool = ('toolchain-xtensa/bin/xtensa-lx106-elf-size' if esp8266 else
                'toolchain-xtensa-esp32/bin/xtensa-esp32-elf-size')
        pio_dir = Path.home() / '.platformio'
        pio_dir = Path(os.environ.get('PLATFORMIO_CORE_DIR', str(pio_dir)))
        directory = ROOT / '.pio/build' / environment
        if not (directory / 'firmware.elf').is_file() or not (directory / 'firmware.bin').is_file():
            sys.exit(f'Build {environment} first: pio run -e {environment}')
        result = subprocess.check_output(
            [str(pio_dir / 'packages' / tool), '-A', str(directory / 'firmware.elf')],
            text=True)
        sections = {}
        for row in result.splitlines():
            fields = row.split()
            if len(fields) >= 2 and fields[0].startswith('.') and fields[1].isdigit():
                sections[fields[0]] = int(fields[1])
        dram_sections = ('.data', '.rodata', '.bss', '.noinit') if esp8266 else (
            '.dram0.data', '.dram0.bss', '.noinit')
        iram_sections = ('.text', '.text1') if esp8266 else ('.iram0.vectors', '.iram0.text')
        dram = sum(sections.get(name, 0) for name in dram_sections)
        iram = sum(sections.get(name, 0) for name in iram_sections)
        image = (directory / 'firmware.bin').stat().st_size
        if not dram or not iram or not image:
            sys.exit(f'Incomplete linked image for {environment}')
        print(f'{environment}: static_dram={dram} iram={iram} image={image}')
        for name, actual in [('static_dram', dram), ('iram', iram), ('image', image)]:
            if actual > profile[name]:
                print(f'  FAIL {name}: {actual} > reviewed ceiling {profile[name]}')
                failed = True
        if args.stock_dram:
            if not esp8266:
                sys.exit('--stock-dram is an ESP8266-only check')
            # This is an optimistic upper bound, before Wi-Fi, trust-anchor,
            # filesystem, allocator fragmentation, and application heap costs.
            available = 81920 - dram
            tls = budgets['esp8266_bounded_tls'] if profile.get('tls_profile') == 'bounded' else budgets['esp8266_core_3_1_2']
            work = budgets['esp8266_phase_workspaces']
            # All allocations below are additional to the linked static image.
            # Stock core frees X509 after handshake; it retains its TLS stack.
            connected = (tls['minimum_link_heap'] - tls['x509_context'] +
                         work['transport'] + work['pending_records'] +
                         work['schedules'])
            phases = {
                'link_handshake': tls['minimum_link_heap'],
                'connected': connected,
                'configuration': connected + work['configuration'],
                'ota': tls['minimum_ota_heap'],
            }
            for phase, required in phases.items():
                print(f'  {phase}: at most {available} heap bytes; minimum {required}')
                if available < required:
                    print(f'  FAIL {phase}: at least {required - available} bytes short')
                    failed = True
            print('  Bounds exclude pre-existing Wi-Fi, trust-anchor and filesystem heap; '
                  'hardware acceptance remains required')
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
