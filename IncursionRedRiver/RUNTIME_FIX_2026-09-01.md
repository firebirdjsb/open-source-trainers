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
- Living AI use `SkeletalMeshComponentBudgeted`; its base component-space array can remain empty until ragdoll/death. Bone ESP now falls back to the authoritative `IRRBodyComponent` eye, thorax, stomach, arm, leg, and foot locations.
- Those pose functions are sampled in bounded batches on the window/game thread and cached for the render thread. Actor-location delta compensation keeps cached limbs attached between samples.
- The optimized fallback uses one `GetMainBoneLocations` call per visible enemy instead of nine separate reflected calls. Its small engine output buffer is reused, off-screen actors are excluded, and the slower compatibility/first-label path is capped at 4 Hz and two actors per pass to prevent initialization hitches.
- Cached poses are bound to the actor's current `BodyComponent` and require a live actor-root location before rendering. Synthetic neck/pelvis joints connect only the validated game-provided anchors.

## Aimbot visibility
- `Controller::LineOfSightTo` no longer runs from the DX12 Present path. Up to eight shortlisted targets are sampled on the window/game thread and consumed through a 250 ms cache.
- `bAlternateChecks` is disabled so Unreal executes its full pawn target/head/side-point LOS path rather than the reduced alternate pass that rejected open targets.
- A fresh RMB press invalidates older visibility results; acquisition waits for a current sample and still rejects confirmed occluded targets.
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

The dump includes hook/renderer state, UE world chain, GUObjectArray, ProcessEvent, team/hostiles, weapon/subobjects, stamina values, inventory/stash container probes, last item insertion backend/results, raw and budgeted live-pose skeletal caches, LOS cache/thread state, up to 64 active characters with bone-point sources, aimbot state, and key current offsets.

## UI cleanup
- Increased and explicitly laid out the header so HOOK / QUEUE / DX12 / WORLD / PAWN no longer clips at the bottom.
- Removed `Local player weapon sounds` and `Third-person sounds only` options and their reapply state.
