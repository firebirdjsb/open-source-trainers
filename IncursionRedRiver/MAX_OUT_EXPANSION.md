# Incursion: Red River - Max-Out Expansion Checkpoint

This source tree is based on the latest local-Codex project supplied on 2026-09-01.
The already-working DX12 renderer, live world resolver, ESP, aimbot and established
player/weapon cheats were deliberately left intact. Expansion work is isolated in
new feature modules or narrow read-only integration points.

## Added in this pass

### Safe item spawner
- 113 current-dump `ID_*` package entries generated from the fresh Dumper-7 output.
- Search + category filtering.
- Item/resource quantity controls.
- Medical, M4 and AK quick kits.
- Uses `GeneralGameInstance.AddItem` / `AddResource`; no guessed inventory writes.

### Loot magnet
- Replaces the old disabled/guessed Item Magnet implementation.
- Resolves the fresh dump's real `Test_C.PickUpActor` UClass at object index `0x111D`.
- Reuses the existing cached `ULevel::Actors` list; it does not run an independent world/UObject scan.
- Moves pickups with validated `AActor.K2_SetActorLocation` (`0x296F`).
- Opt-in continuous mode, 100-1000 ms refresh, configurable range and a hard 64-pickup/pass cap.
- Pickups already at the player's feet are ignored to avoid physics jitter.

### Movement+
- Heal / set health percentage through `SimpleGameplayAttribute.SetBaseValue`.
- No fall damage with original-value restoration.
- Personal actor time dilation.
- Jump, gravity, air control, acceleration, friction, step-height, slope, swim and custom-movement tuning.
- Teleport forward/up/down and world-bound position bookmark/return.
- Persistent field edits capture and restore their original values.
- Gravity tuning yields to the existing Fly/NoClip implementation.

### Tactical overlay
- 2D radar.
- Off-screen hostile direction indicators.
- Proximity warning.
- Optional line to current aimbot target.
- Reuses the existing character cache; no extra LOS spam.
- Own position snapshot defaults to 10 Hz and is configurable from 50-500 ms.

### World / mission tools
- Force extraction, kill/show AI, complete/fail/regenerate tracked missions.
- Time of day and weather calls.
- AI optimization + optimization level.
- Debug operations, extended party, vendor restock state, mission success rate and weapon durability controls.
- Built-in cheat movement, cinematic camera, weapon-sound controls.
- Low-Lumen scalability toggle using the game's own `SetLumenScalability` function.
- Destructive reset functions are behind an explicit `RESET` confirmation gate.

## Optimization rules

- Existing `GameAccess::Refresh()` remains the sole expensive world/UObject acquisition path.
- Tactical and loot systems reuse existing caches instead of independently scanning memory.
- Tactical position snapshot: 100 ms default.
- Loot magnet: 250 ms default and max 24 teleports/pass.
- Movement+ writes: 100 ms cadence only when enabled.
- Stateful world/debug toggles: at most once per second while enabled.
- One-shot buttons have no background cost.
- Disabled new systems return immediately.

## Dump/static validation

Run from the project root:

```text
python tools/verify_dump_offsets.py
python tools/verify_fresh_dump_layout.py
```

Current results in this source tree:
- Reflected property offsets: 115 PASS.
- Native fresh-dump layout: 50,035 module-backed objects, zero added/removed, one ASLR relocation delta PASS.
- Validated current object/UFunction indices: 88 PASS.
- New feature translation units were checked with C++17 syntax validation.

## Runtime validation still required

New calls/fields are dump-validated but must still be exercised in the current Windows game build.
Build `Release | x64`, then test new modules one at a time. If a new feature misbehaves,
disable that feature and capture the Diagnostics tab rather than changing the working renderer,
ESP or aimbot as a first response.

## UI polish + input ownership + inventory/economy repair

This pass keeps the working DX12/ESP/aim paths intact and changes only the UI/input integration plus the broken item/economy backends.

### Professional UI shell
- Replaced the crowded top tab bar with a dark professional control-center layout.
- Added grouped left-side navigation, a persistent runtime-status header and a scrollable workspace.
- Added a consistent blue/slate visual theme, rounded panels/controls and an explicit menu-input-lock footer.

### Menu input priority
- While the menu is open the hooked WndProc consumes keyboard, mouse and `WM_INPUT` rather than relying on `ImGuiIO::WantCapture*`.
- HOME/INSERT/DELETE are never forwarded to the game.
- Mouse capture/clipping is released while the overlay is open and ImGui renders its software cursor above the game.
- The local controller receives balanced `SetIgnoreLookInput`/`SetIgnoreMoveInput` calls, movement is stopped once, and the pawn input stack is disabled with `AActor::DisableInput` until the menu closes.
- Input is restored once on close/shutdown. These calls are transition-based; they are not spammed every frame.
- Aimbot keeps drawing its FOV circle for tuning but will not submit aim input while the menu owns the mouse.

### Inventory / stash / currency backend repair
- Catalog entries now track both the package index and the actual live `IRRItemDefinition` index.
- Player inventory resolves through the existing live diagnostics and `InventoryFunctionLibrary.GetPlayerInventoryComponent` fallback.
- Stash resolves through `InventoryFunctionLibrary.GetStashInventoryComponent` on the live BlueprintFunctionLibrary CDO.
- Player insertion prefers `InventoryFunctionLibrary.AddItemByRowName` using the definition object's FName.
- Both destinations can fall back through `GeneralGameInstance.AddItem`, again using the definition FName rather than the old package FName.
- Destination-specific fallback uses `InventoryComponent.AddDefaultItem` and reflected `MainContainers`.
- Every inventory/stash insertion is accepted only after the actual `MainContainers` item count increases. A successful ProcessEvent dispatch alone is no longer shown as success.
- Inventory refresh submits `ItemsUpdated`, `UpdateCurrency` and `OnRep_MainContainers` after mutations.
- Cash and Marked Coin now use the same verified item path. Profile currency defaults to an auto mode that tries stash first and then player inventory, accepting only an observed change.

### Validation
- `verify_dump_offsets.py` now includes `InventoryComponent::MainContainers`.
- `verify_fresh_dump_layout.py` now verifies the input-priority and inventory UFunctions plus the InventoryFunctionLibrary CDO.
- `verify_item_catalog.py` verifies all 113 package indices and all 113 live `IRRItemDefinition` indices against the fresh dump.
