# Aimbot / ESP / Infinite Ammo test revision

This revision follows the first successful in-game DX12 test where the menu and FOV circle rendered correctly.

## What to test

1. **Aimbot**
   - Enable Aimbot and leave `Use Mouse Input Aim` enabled.
   - Put an enemy inside the FOV circle. A gold dot should appear on the selected target point.
   - Hold **Right Mouse Button**. The view should move toward the gold target point.
   - If movement is too weak/strong, adjust `Aim Strength`; `Smooth Amount` controls convergence speed.
   - `Use Mouse Input Aim = OFF` tests the dump-confirmed `SetControlRotation` UFunction path.
   - Diagnostics shows whether the target came from a live body point or capsule fallback plus counts for every acquisition stage.

2. **Infinite Ammo**
   - Enable Infinite Ammo in the Weapon tab.
   - `Game infinite-ammo flag` should immediately show `ON`.
   - Fire the currently equipped weapon and confirm rounds are not consumed.
   - `Inventory GetInfiniteAmmunition` should also show `ON`; the weapon snapshot remains diagnostic only.

3. **ESP movement stability / Fly / Invisible to AI**
   - Run and jump while watching a nearby enemy. Boxes and text should remain anchored because final coordinates now come from Unreal's viewport projection.
   - Fly uses WASD, Space to ascend, and Ctrl to descend. Disable it and confirm normal gravity/movement return.
   - Invisible to AI should show a valid non-zero stimulus component and `invisible flag: ON`.

4. **Runtime-chain diagnosis**
   - Enter an active raid before judging the selected world.
   - Open **Diagnostics** and check the first-failure line.
   - Confirm the selected world, persistent level, owning game instance, local-player array, local player, controller, and pawn are all non-zero and correctly typed.
   - In a menu, `resolved active World` is expected to remain zero even if raw `GWorld` is non-zero; raid character evidence is required before a world is published.
   - A null local pawn no longer prevents independent `IRRBaseCharacter` actor discovery.

## Dump-backed offsets added in this revision

- `SceneComponent::AttachParent` = `0xC8`
- `SceneComponent::RelativeRotation` = `0x158`
- `SceneComponent::RelativeScale3D` = `0x170`
- `GeneralGameInstance::bInfiniteAmmunition` = `0x24D`

`tools/verify_dump_offsets.py` verifies 119 reflected fields, and `tools/verify_fresh_dump_layout.py` validates the native layout plus 99 stable class/function indices against the fresh dump.

## Safety constraints in this revision

- `WeaponInHands + 0x680` is consumed only when the object is first validated as `BP_MasterWeapon`.
- `BP_MasterWeapon::WeaponComponent + 0x358` is consumed only after component type validation; typed instance/outer-chain discovery is the fallback.
- Infinite Ammo never writes the transient `AmmunitionState` snapshot.
- Enemy selection comes from the local `IRRTeamComponent::Hostiles` array. ESP shows amber typed candidates only when that list is unavailable; aimbot never targets those candidates.
- The DX12 hook and renderer were not changed as part of the runtime-object repair.
