#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / 'IncursionCheat_DX12' / 'Features' / 'ItemCatalog.h'
INVENTORY_SERVICE = ROOT / 'IncursionCheat_DX12' / 'Features' / 'InventoryService.cpp'
DUMP = ROOT / 'IncursionCheat_DX12' / '5.6.1-0+UE5-Test_C' / 'GObjects-Dump.txt'
ENTRY_RE = re.compile(r'\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*"([^"]+)"\s*\}')
PRESET_RE = re.compile(
    r'\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\s*\}\s*,?\s*//\s*(.+)'
)
DUMP_RE = re.compile(r'^\[([0-9A-Fa-f]+)\]\s+\{0x[0-9A-Fa-f]+\}\s+(.*)$')

if not CATALOG.exists() or not INVENTORY_SERVICE.exists() or not DUMP.exists():
    print('ERROR: catalog, inventory service, or fresh dump missing', file=sys.stderr)
    sys.exit(2)

objects = {}
for line in DUMP.read_text(errors='replace').splitlines():
    m = DUMP_RE.match(line)
    if m:
        objects[int(m.group(1), 16)] = m.group(2)

entries = [(int(a, 16), int(b, 16), name) for a, b, name in ENTRY_RE.findall(CATALOG.read_text())]
presets = [
    (int(definition, 16), int(preset, 16), label.strip())
    for definition, preset, label in PRESET_RE.findall(INVENTORY_SERVICE.read_text())
]
failures = []
for package_index, definition_index, item_id in entries:
    package = objects.get(package_index)
    definition = objects.get(definition_index)
    if package != f'Package {item_id}':
        failures.append(f'{item_id}: package 0x{package_index:X} -> {package!r}')
    expected_definition = f'IRRItemDefinition {item_id}.{item_id}'
    if definition != expected_definition:
        failures.append(f'{item_id}: definition 0x{definition_index:X} -> {definition!r}')

catalog_definitions = {definition for _, definition, _ in entries}
for definition_index, preset_index, label in presets:
    definition = objects.get(definition_index)
    preset = objects.get(preset_index)
    if definition_index not in catalog_definitions:
        failures.append(f'{label}: definition 0x{definition_index:X} is not in ItemCatalog')
    if not definition or not definition.startswith('IRRItemDefinition '):
        failures.append(f'{label}: definition 0x{definition_index:X} -> {definition!r}')
    if not preset or not preset.startswith('IRRItemPreset '):
        failures.append(f'{label}: preset 0x{preset_index:X} -> {preset!r}')

if failures:
    print('ITEM CATALOG VERIFICATION FAILED')
    for failure in failures:
        print(' -', failure)
    sys.exit(1)

print(f'PASS: verified {len(entries)} item catalog entries and {len(presets)} complete-weapon presets')
