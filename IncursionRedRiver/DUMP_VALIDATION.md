# Updated Dumper-7 validation — 2026-09-01

The updated live-raid dump supplied from the current Incursion: Red River installation identifies the game as:

- `Test_C`
- `5.6.1-0+UE5`

## Result

The updated `GObjects-Dump.txt` at `C:\Dumper-7\5.6.1-0+UE5-Test_C\` was compared against the project baseline. Its `GObjects-Dump-WithProperties.txt` was also checked directly against every reflected field consumed by the runtime.

- Previous module-backed UObject entries: **50,035**
- Fresh module-backed UObject entries: **50,035**
- Added native/module-backed object names: **0**
- Removed native/module-backed object names: **0**
- Address relocation deltas observed: **1**
- Uniform relocation delta: **-0x58EF0000**
- Reflected runtime fields checked: **94**
- Stable live UClass/UFunction indices checked: **34**

All 50,035 module-backed reflected objects moved by exactly the same amount. Key types such as `UWorld`, `APlayerController`, `APlayerCameraManager`, `IRRBaseCharacter`, `HealthComponent`, `AdvancedMovementComponent`, `WeaponComponent`, and `AmmunitionState` also match exactly.

This proves that the reflected/native class layout used by the repaired runtime is unchanged. The different absolute addresses are expected because of ASLR.

This comparison does not validate unreflected globals by itself. The old `GObjects` RVA was stale even though the native class image matched. The current `GObjects` RVA (`0xB72B780`) was recovered from the live process and independently validated as a chunked `GUObjectArray` using five stable UObject indices. The DLL repeats that structural validation at runtime and falls back to scanning writable PE sections if the configured RVA stops matching.

## Generated SDK limitation

The updated folder contains both GObjects dumps, including the property-rich version. That property-rich dump directly confirms the field offsets used by `sdk/Offsets.h`.

However, `CppSDK/SDK.hpp` references generated `CppSDK/SDK/*.hpp` files that are absent, and `PropertyFixup.hpp` is empty. The dump is therefore suitable for property/index validation but is not a self-contained compilable Dumper-7 SDK.

## Verification commands

From the project root with Python 3:

```text
python tools\verify_dump_offsets.py
python tools\verify_fresh_dump_layout.py

python tools\verify_dump_offsets.py --dump-dir "C:\Dumper-7\5.6.1-0+UE5-Test_C"
python tools\verify_fresh_dump_layout.py --fresh-dir "C:\Dumper-7\5.6.1-0+UE5-Test_C"
```

All commands should print `PASS`; the second pair explicitly checks the updated external dump.

If a future Dumper-7 run causes `verify_fresh_dump_layout.py` to fail, do not assume the current RVAs or offsets remain valid.
