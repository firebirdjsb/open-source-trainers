# Runtime Fix Pass - 2026-09-01

This build preserves the working DX12 renderer, command queue hook, menu input capture, world resolver, box ESP, and aimbot controls.

## Infinite Stamina crash fix
- Removed raw writes to `FirstPersonStamina::RecoverPerSecond`.
- Removed per-frame raw writes to BaseData / CurrentData / OldData stamina snapshots.
- Uses only validated reflected `GetCurrentStamina`, `GetCurrentValue`, `GetCurrentMaxValue`, and throttled `SetBaseValue`.
- Only the single validated local stamina attribute is touched.
- The mutation is capped at 5 Hz and performs sanity checks before dispatch.

## Aimbot acquisition
- Target selection is driven by crosshair distance and sticky locking, with no secondary asynchronous acquisition gate.
- Selected body parts use the authoritative `IRRBodyComponent` head, thorax, stomach, arm, leg, and foot locations sampled in bounded batches on the window/game thread.
- Unsmoothed mouse-input mode remains mouse input instead of switching unexpectedly to a direct controller call.

## Item spawner / inventory
- Removed the unverified Hideout Resource destination from the item spawner UI.
- Item insertion now exposes MainContainers validity/count and item record counts.
- `EDefaultItemType` probes definition-oriented value 1 first, then compatibility value 0.
- `AddDefaultItem`'s real game-built ContainerItem return is captured.
- The returned item is checked with the game's `CanAddToInventory`.
- Only if the game says it is valid is it passed to `TryAddItem`.
- Success is still accepted only after the requested live IRRItemDefinition count increases.

## Full runtime diagnostic dump
Diagnostics -> `CREATE FULL RUNTIME DUMP` or the fixed `WRITE DIAGNOSTIC DUMP` title-bar button

Writes next to `Test_C-Win64-Shipping.exe`:
`IncursionCheat_FullDiagnostics.txt`

The dump includes hook/renderer state, UE world chain, GUObjectArray, ProcessEvent, team/hostiles, weapon/subobjects, stamina values, inventory/stash container probes, last item insertion backend/results, live body-point sampling, active-character counts, aimbot state, and key current offsets.

## UI cleanup
- Increased and explicitly laid out the header so HOOK / QUEUE / DX12 / WORLD / PAWN no longer clips at the bottom.
- Removed `Local player weapon sounds` and `Third-person sounds only` options and their reapply state.
