# Runtime Fix Pass - 2026-09-01

This build preserves the working DX12 renderer, command queue hook, menu input capture, world resolver, box ESP, and existing aimbot architecture.

## Infinite Stamina crash fix
- Removed raw writes to `FirstPersonStamina::RecoverPerSecond`.
- Removed per-frame raw writes to BaseData / CurrentData / OldData stamina snapshots.
- Uses only validated reflected `GetCurrentStamina`, `GetCurrentValue`, `GetCurrentMaxValue`, and throttled `SetBaseValue`.
- Only the single validated local stamina attribute is touched.
- The mutation is capped at 5 Hz and performs sanity checks before dispatch.

## Bone ESP
- Fixed `CachedComponentSpaceTransforms` validation: TArray allocation capacity may be >256 even when live bone Count is valid.
- Bone iteration is separately capped while TArray capacity is allowed up to a conservative 1024.
- Native reference-skeleton parent indices remain preferred.
- If parent topology cannot be verified, an anatomical chain is constructed from the real cached transform points rather than guessed world offsets.

## Item spawner / inventory
- Removed the unverified Hideout Resource destination from the item spawner UI.
- Item insertion now exposes MainContainers validity/count and item record counts.
- `EDefaultItemType` probes definition-oriented value 1 first, then compatibility value 0.
- `AddDefaultItem`'s real game-built ContainerItem return is captured.
- The returned item is checked with the game's `CanAddToInventory`.
- Only if the game says it is valid is it passed to `TryAddItem`.
- Success is still accepted only after the requested live IRRItemDefinition count increases.

## Full runtime diagnostic dump
Diagnostics -> `WRITE FULL DIAGNOSTIC DUMP`

Writes next to `Test_C-Win64-Shipping.exe`:
`IncursionCheat_FullDiagnostics.txt`

The dump includes hook/renderer state, UE world chain, GUObjectArray, ProcessEvent, team/hostiles, weapon/subobjects, stamina values, inventory/stash container probes, last item insertion backend/results, skeletal cache, up to 64 active characters with bone-point counts, aimbot state, and key current offsets.

## UI cleanup
- Increased and explicitly laid out the header so HOOK / QUEUE / DX12 / WORLD / PAWN no longer clips at the bottom.
- Removed `Local player weapon sounds` and `Third-person sounds only` options and their reapply state.
