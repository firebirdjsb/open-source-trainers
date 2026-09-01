#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / 'IncursionCheat_DX12' / 'Features' / 'ItemCatalog.h'
DUMP = ROOT / 'IncursionCheat_DX12' / '5.6.1-0+UE5-Test_C' / 'GObjects-Dump.txt'
ENTRY_RE = re.compile(r'\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*"([^"]+)"\s*\}')
DUMP_RE = re.compile(r'^\[([0-9A-Fa-f]+)\]\s+\{0x[0-9A-Fa-f]+\}\s+(.*)$')

if not CATALOG.exists() or not DUMP.exists():
    print('ERROR: catalog or fresh dump missing', file=sys.stderr)
    sys.exit(2)

objects = {}
for line in DUMP.read_text(errors='replace').splitlines():
    m = DUMP_RE.match(line)
    if m:
        objects[int(m.group(1), 16)] = m.group(2)

entries = [(int(a, 16), int(b, 16), name) for a, b, name in ENTRY_RE.findall(CATALOG.read_text())]
failures = []
for package_index, definition_index, item_id in entries:
    package = objects.get(package_index)
    definition = objects.get(definition_index)
    if package != f'Package {item_id}':
        failures.append(f'{item_id}: package 0x{package_index:X} -> {package!r}')
    expected_definition = f'IRRItemDefinition {item_id}.{item_id}'
    if definition != expected_definition:
        failures.append(f'{item_id}: definition 0x{definition_index:X} -> {definition!r}')

if failures:
    print('ITEM CATALOG VERIFICATION FAILED')
    for failure in failures:
        print(' -', failure)
    sys.exit(1)

print(f'PASS: verified {len(entries)} item catalog entries against fresh package + IRRItemDefinition objects')
