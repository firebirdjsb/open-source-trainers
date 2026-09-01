# IncursionCheat DX12 — repaired source

This project is a cleaned/repaired version of the supplied Incursion: Red River DX12 internal project.

## Build

Open `IncursionCheat_DX12.sln` in Visual Studio 2022 and select **Release | x64**. Build the solution. The loader and DLL are separate projects in the solution.

The loader resolves the DLL relative to itself and performs ASLR-safe remote `LoadLibraryW` resolution rather than relying on the original hard-coded E: drive path.

## Loader behavior

Run `Loader.exe` before or after starting the game. With no arguments it now waits indefinitely for `Test_C-Win64-Shipping.exe`, waits for the target's x64 module loader to become ready, allows a two-second startup settling period, and then injects the DLL beside the loader.

Useful options:

```text
Loader.exe --timeout 300
Loader.exe --no-wait
Loader.exe --game "C:\path\to\Test_C-Win64-Shipping.exe"
Loader.exe --delay 5
```

The loader validates that the input is an x64 PE DLL, checks the target architecture, resolves the remote owner/RVA of `LoadLibraryW`, uses bounded thread waits, and verifies that the requested DLL appears in the target module list. Running it a second time reports the already-loaded module rather than injecting a duplicate. If the game is elevated and `OpenProcess` is denied, run the loader at the same elevation level.

## Current dump validation

A baseline Dumper-7 dump is included under:

`IncursionCheat_DX12/5.6.1-0+UE5-Test_C/`

The updated **2026-09-01** live-raid dump supplied at `C:\Dumper-7\5.6.1-0+UE5-Test_C\` was validated without modifying it. All **94** consumed reflected fields, **50,035** module-backed UObject definitions, and **34** stable class/function indices match the project baseline. See `DUMP_VALIDATION.md` for the commands and detailed comparison.

The updated folder includes a complete property-rich GObjects dump, so the consumed field offsets are now independently confirmed by the new run. Its referenced `CppSDK/SDK/*.hpp` files are still absent, so the generated C++ SDK itself is incomplete.

## Runtime acquisition and diagnostics

`HOME`, `INSERT`, or `DELETE` toggle the menu.

The DX12 hook/renderer path is unchanged. A dedicated **Diagnostics** tab now displays the complete live chain in hexadecimal:

`GWorld -> OwningGameInstance -> LocalPlayers -> LocalPlayer -> PlayerController -> Pawn`

It also shows the selected world and its source, viewport/engine fallbacks, `GUObjectArray` layout, actor array, equipped weapon/components, local AI stimulus component, skeletal transform array, sampled translations, reference-parent discovery, every aimbot rejection stage, and whether an RMB aim submission changed control rotation.

The configured `GUObjectArray` RVA is structurally checked against five known UObject indices before it is trusted. If that check fails, the runtime locates and validates the array in writable PE sections. Diagnostics shows the resolved root, source, probe score, object count, and typed-object bucket counts. State changes are also written at a bounded rate to `incursion_cheat_fixed.log`, so the failing chain can be recovered even without a screenshot.

`GWorld` is no longer accepted only because it is non-null. Candidate worlds are type-checked and scored using their persistent level, owning-world relationship, actor array, game state/mode, viewport association, game instance, and live `IRRBaseCharacter` population. Menu/transient candidates are not published as the active world without live IRR character evidence. Local-player discovery can fall back through the engine, viewport, controller, and `GUObjectArray` paths when `World + 0x228` is null.

## Validated gameplay test features

The original project contained guessed raw writes for spawning, item magnet behavior, spread, and fake UE console commands. Those unsafe implementations remain disabled. The Player menu now exposes reversible features only where the updated dump provides an exact reflected field or function route: fly, infinite stamina, runtime recoil suppression, arm-sway suppression, no weapon malfunctions/durability loss, high carry weight, AI-stimulus invisibility, and bullet debug traces.

Weapon ammunition support is marked experimental until it is confirmed in-game. Live executable disassembly identified the UE5.6 `ProcessEvent` vtable slot as `0x4C`; the former `0x4D` slot was a constant-return stub and explained the failed ammo/invisibility calls. Ammo invokes the game-instance and inventory setters, verifies the result with `InventoryComponent::GetInfiniteAmmunition`, and uses the inventory toggle as a fallback. The transient `AmmunitionState` snapshot is read for diagnostics and is never written.

## First test

Build **Release | x64**, launch the game normally, then use the loader. Open **Diagnostics** after entering a raid and capture the full tab if a stage fails. The first-failure text and hexadecimal addresses are intended to make the next pointer/layout failure actionable.


## Visual Studio build fixes (2026-08-31)

- Added the required local declaration for `ImGui_ImplWin32_WndProcHandler`.
- Replaced the project logger's deprecated `fopen` call with `fopen_s`.
- Suppressed Release-only unused-HRESULT warnings in the bundled ImGui DX12 backend.
- The Loader still depends on the DLL for build ordering, but no longer links against `IncursionCheat_DX12.lib`; it injects the DLL dynamically and requires no import library.
- Added `_CRT_SECURE_NO_WARNINGS` to both MSVC projects as a fallback for bundled third-party code.

## Gameplay fixes after first successful in-game render test (2026-08-31)

The first in-game test confirmed the DX12 overlay/FOV renderer works, but exposed three gameplay-layer problems. This revision changes those paths as follows:

### Aimbot
- Target selection prefers the live `USkeletalMeshComponent::CachedComponentSpaceTransforms` array (`0x9B8`). When the runtime reports an empty bone cache, a clearly diagnosed capsule-relative target keeps enemy acquisition operational instead of returning `Target: NONE` for every actor.
- Bone component-space positions are converted to world space through the reflected `USceneComponent::AttachParent`, `RelativeLocation`, `RelativeRotation`, and `RelativeScale3D` chain.
- Default aim application now uses relative Win32 mouse-look input while RMB is held because the game's Enhanced Input/camera update overwrites a raw `AController::ControlRotation` write.
- Direct rotation mode calls the dump-confirmed `Controller::SetControlRotation` UFunction and retains a raw field write only as a fallback.
- When skeletal validation succeeds, aimbot consumes the exact verified head/chest position produced by the Bone ESP path.
- The Diagnostics tab records scan, hostile, living, range, projection, and FOV counts; target source; RMB state; submission status; and next-frame control-rotation observation.

### Skeleton / Bone ESP
- Added an actual `Skeleton / Bone ESP` toggle.
- Reads live cached skeletal transforms rather than hardcoded bone offsets.
- Validates the transform array pointer, count, capacity, and initial translations before consuming it.
- Discovers and validates the skeletal asset's actual reference-skeleton parent-index array. No nearest-previous-bone approximation is used; if parents cannot be verified, points are shown without invented connections.
- The ESP tab reports how many actors/bone points were resolved during the most recent frame, which makes failed skeletal traversal obvious during testing.
- Character enumeration is independent of the local-pawn chain and class-checks `IRRBaseCharacter` instances, so ESP diagnostics can proceed while local-player discovery is still failing.
- A bounded skeletal sample is validated during runtime refresh before camera projection, so mesh/transform counts can become non-zero even while controller/camera acquisition is failing.
- Confirmed enemies come from the local character's reflected `IRRTeamComponent::Hostiles` array. Unknown typed characters are amber ESP diagnostics only and are never eligible for aimbot selection.
- ESP final coordinates now use `PlayerController::ProjectWorldLocationToScreen`, so boxes/text share Unreal's current viewport projection instead of a camera-cache frame that can bob while the local player runs or jumps.
- Expensive health/name/bone work is performed only for sorted, on-screen, distance-filtered candidates. The default is capped at 25 actors, skeleton ESP defaults off, and the large object-classification pass backs off after pawn acquisition to remove periodic hitches.

### Infinite Ammo
- The old implementation wrote `WeaponComponent::AmmunitionState.MagCapacity`, but that structure is only a transient snapshot and the game overwrites it.
- The dump exposes `Test_C.GeneralGameInstance::bInfiniteAmmunition` at `0x24D`, together with game-instance and inventory get/set/toggle functions.
- Infinite Ammo resolves and type-validates the live `GeneralGameInstance`, controller, pawn, inventory component, weapon component, and equipped weapon before enabling the game-owned state. It calls both dump-confirmed ammo functions and restores the previous state when disabled.
- The old ammunition-state/chamber/HUD fallback write has been removed; those fields are diagnostic snapshots only.
- The Weapon tab shows both the live game-instance flag and the value returned by `InventoryComponent::GetInfiniteAmmunition`.

### Invisible to AI and Fly
- Invisible to AI resolves the local character's typed `SenseStimulusComponent`, calls `SetInvisible_Server`, maintains the reflected `bInvisible` state, and also invokes the game-instance toggle. Original state is restored when disabled.
- Fly validates and backs up `GravityScale`, `MovementMode`, `MaxFlySpeed`, `BrakingDecelerationFlying`, and `Velocity`; it uses UE's flying mode with Space/Ctrl vertical movement and restores all saved fields on disable.

## Verification status

- `Release | x64` clean rebuild: DLL and loader succeeded with zero compiler/linker errors.
- `tools/verify_dump_offsets.py`: 94 reflected fields verified.
- `tools/verify_fresh_dump_layout.py`: 50,035 module-backed objects and 34 live UClass/UFunction lookup indices verified.
- Loader integration fixture: default wait-before-process, remote DLL load/signaling, module-list verification, and duplicate-injection detection all passed in an x64 local host.
- Gameplay behavior still requires an in-raid test; a successful build and dump match do not prove that runtime object selection is correct for every menu/raid transition.
