#!/usr/bin/env python3
from pathlib import Path
import argparse
import collections
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASELINE_DIR = ROOT / "IncursionCheat_DX12" / "5.6.1-0+UE5-Test_C"

parser = argparse.ArgumentParser(
    description="Compare a fresh Dumper-7 object layout with the project baseline."
)
parser.add_argument(
    "--baseline-dir",
    type=Path,
    default=DEFAULT_BASELINE_DIR,
    help="Directory containing the baseline GObjects-Dump-WithProperties.txt",
)
parser.add_argument(
    "--fresh-dir",
    type=Path,
    default=DEFAULT_BASELINE_DIR,
    help="Directory containing the fresh GObjects-Dump.txt",
)
args = parser.parse_args()
OLD = args.baseline_dir / "GObjects-Dump-WithProperties.txt"
NEW = args.fresh_dir / "GObjects-Dump.txt"
LINE_RE = re.compile(r"^\[[^]]+\]\s+\{0x([0-9A-Fa-f]+)\}\s+(.*)$")
INDEX_RE = re.compile(r"^\[([0-9A-Fa-f]+)\]\s+\{0x[0-9A-Fa-f]+\}\s+(.*)$")

KEYS = [
    "Class Engine.Actor",
    "Class Engine.Character",
    "Class Engine.GameInstance",
    "Class Engine.PlayerController",
    "Class Engine.PlayerCameraManager",
    "Class Test_C.IRRBaseCharacter",
    "Class Test_C.HealthComponent",
    "Class Test_C.AdvancedMovementComponent",
    "Class Test_C.WeaponComponent",
    "ScriptStruct Test_C.AmmunitionState",
]

EXPECTED_INDICES = {
    "Class CoreUObject.Class": 0x001E,
    "Class CoreUObject.Function": 0x001F,
    "Class Engine.Level": 0x0443,
    "Class Engine.World": 0x04B8,
    "Class Engine.PlayerController": 0x076D,
    "Class Engine.GameEngine": 0x07F6,
    "Class Engine.GameInstance": 0x0807,
    "Class Engine.GameViewportClient": 0x0810,
    "Class Engine.LocalPlayer": 0x084C,
    "Class Engine.SkeletalMesh": 0x08A5,
    "Class Engine.SkeletalMeshComponent": 0x025A,
    "Class SenseSystem.SenseStimulusComponent": 0x0B01,
    "Class Test_C.IRRBaseCharacter": 0x0FCE,
    "Class Test_C.IRRAIBaseCharacter": 0x0FCF,
    "Class Test_C.GeneralGameInstance": 0x103C,
    "Class Test_C.IRRBodyComponent": 0x10C3,
    "Class EasyBallistics.EBBarrel": 0x11AE,
    "Class Test_C.FirstPersonStamina": 0x102B,
    "Class Test_C.FirstPersonStamina_Arm": 0x102C,
    "Class Test_C.FirstPersonWeaponRecoil": 0x1032,
    "Class Test_C.InventoryComponent": 0x1086,
    "Class Test_C.IRRTeamComponent": 0x10E8,
    "Class Test_C.WeaponComponent": 0x115F,
    "Class Test_C.SimpleGameplayAttribute": 0x113C,
    "BlueprintGeneratedClass BP_MasterWeapon.BP_MasterWeapon_C": 0x11D44,
    "Class Test_C.PickUpActor": 0x111D,
    "InventoryFunctionLibrary Test_C.Default__InventoryFunctionLibrary": 0x0907C,
    "Package ID_Bandage": 0x0C653,
    "Package ID_Cash": 0x0C817,
    "Package ID_MarkedCoin": 0x0C854,
    "Package ID_Health_Injector": 0x0C85C,
    "Function Engine.PrimitiveComponent.GetCollisionEnabled": 0x1F5E,
    "Function Engine.PrimitiveComponent.SetCollisionEnabled": 0x1FA6,
    "Function Engine.Actor.DisableInput": 0x291F,
    "Function Engine.Actor.EnableInput": 0x2920,
    "Function Engine.Actor.GetActorEnableCollision": 0x2926,
    "Function Engine.Actor.GetActorEyesViewPoint": 0x2927,
    "Function Engine.Actor.SetActorEnableCollision": 0x2994,
    "Function Engine.Actor.K2_SetActorLocation": 0x296F,
    "Function Engine.Controller.LineOfSightTo": 0x29BA,
    "Function Engine.Controller.SetControlRotation": 0x29C4,
    "Function Engine.Controller.SetIgnoreLookInput": 0x29C5,
    "Function Engine.Controller.SetIgnoreMoveInput": 0x29C6,
    "Function Engine.Controller.StopMovement": 0x29C8,
    "Function EasyBallistics.EBBarrel.CalculateAimDirectionFromLocation": 0x2B75,
    "Function Engine.PlayerController.ProjectWorldLocationToScreen": 0x2A36,
    "Function Engine.CharacterMovementComponent.SetMovementMode": 0x527E,
    "Function SenseSystem.SenseStimulusComponent.SetInvisible_Server": 0x5FC4,
    "Function Test_C.GeneralGameInstance.CompleteTrackedMissions": 0x6B70,
    "Function Test_C.GeneralGameInstance.FailTrackedMissions": 0x6B74,
    "Function Test_C.GeneralGameInstance.ForceExtraction": 0x6B76,
    "Function Test_C.GeneralGameInstance.KillAllAI": 0x6B80,
    "Function Test_C.GeneralGameInstance.RegenerateMissions": 0x6B84,
    "Function Test_C.GeneralGameInstance.ResetAchievements": 0x6B86,
    "Function Test_C.GeneralGameInstance.ResetInventory": 0x6B87,
    "Function Test_C.GeneralGameInstance.ResetMissionSystem": 0x6B88,
    "Function Test_C.GeneralGameInstance.ResetRaidData": 0x6B89,
    "Function Test_C.GeneralGameInstance.ResetResources": 0x6B8A,
    "Function Test_C.GeneralGameInstance.ResetTrackedMissions": 0x6B8C,
    "Function Test_C.GeneralGameInstance.ResetVendor": 0x6B8D,
    "Function Test_C.GeneralGameInstance.ResetWorld": 0x6B8E,
    "Function Test_C.GeneralGameInstance.SetAIOptimizationLevel": 0x6B8F,
    "Function Test_C.GeneralGameInstance.SetDurability": 0x6B90,
    "Function Test_C.GeneralGameInstance.SetRestock": 0x6B96,
    "Function Test_C.GeneralGameInstance.SetSuccessRate": 0x6B97,
    "Function Test_C.GeneralGameInstance.SetTimeOfDay": 0x6B98,
    "Function Test_C.GeneralGameInstance.SetWeatherType": 0x6B9A,
    "Function Test_C.GeneralGameInstance.ShowAllAI": 0x6B9B,
    "Function Test_C.GeneralGameInstance.ToggleAIOptimization": 0x6BA2,
    "Function Test_C.GeneralGameInstance.ToggleCheatMovementSpeed": 0x6BA4,
    "Function Test_C.GeneralGameInstance.ToggleCinematicCamera": 0x6BA5,
    "Function Test_C.GeneralGameInstance.ToggleDebugOperations": 0x6BA6,
    "Function Test_C.GeneralGameInstance.ToggleExtendedParty": 0x6BA7,
    "Function Test_C.GeneralGameInstance.ToggleLocalPlayerWeaponSounds": 0x6BAB,
    "Function Test_C.GeneralGameInstance.ToggleTPSoundsOnly": 0x6BAC,
    "Function Test_C.GeneralGameInstance.ToggleBulletDebugTraces": 0x6BA3,
    "Function Test_C.GeneralGameInstance.ToggleInfiniteAmmunition": 0x6BA8,
    "Function Test_C.GeneralGameInstance.ToggleInvisible": 0x6BA9,
    "Function Test_C.GeneralGameInstance.ToggleInvulnerable": 0x6BAA,
    "Function Test_C.GeneralGameInstance.AddItem": 0x6B6E,
    "Function Test_C.GeneralGameInstance.AddResource": 0x6B6F,
    "Function Test_C.GeneralGameInstance.SetFactionReputation": 0x6B91,
    "Function Test_C.GeneralGameInstance.SetLevel": 0x6B93,
    "Function Test_C.GeneralGameInstance.SetLumenScalability": 0x6B94,
    "Function Test_C.GeneralGameInstance.SetOverallFactionReputation": 0x6B95,
    "Function Test_C.FirstPersonStamina.GetCurrentStamina": 0x6B0D,
    "Function Test_C.InventoryComponent.AddDefaultItem": 0x6C1A,
    "Function Test_C.InventoryComponent.ItemsUpdated": 0x6C78,
    "Function Test_C.InventoryComponent.OnRep_MainContainers": 0x6C80,
    "Function Test_C.InventoryComponent.UpdateCurrency": 0x6CB3,
    "Function Test_C.InventoryComponent.SetInfiniteAmmunition": 0x6C9E,
    "Function Test_C.InventoryComponent.GetInfiniteAmmunition": 0x6C55,
    "Function Test_C.InventoryComponent.ToggleInfiniteAmmunition": 0x6CA1,
    "Function Test_C.IRRBodyComponent.GetBodyPartLocation": 0x6DBF,
    "Function Test_C.IRRBodyComponent.GetEyeLocation": 0x6DC0,
    "Function Test_C.SimpleGameplayAttribute.GetCurrentMaxValue": 0x6EEF,
    "Function Test_C.SimpleGameplayAttribute.GetCurrentValue": 0x6EF1,
    "Function Test_C.SimpleGameplayAttribute.SetBaseValue": 0x6EF7,
    "Function Test_C.InventoryFunctionLibrary.AddItemByRowName": 0x6F99,
    "Function Test_C.InventoryFunctionLibrary.GetPlayerInventoryComponent": 0x6FBA,
    "Function Test_C.InventoryFunctionLibrary.GetStashInventoryComponent": 0x6FBD,
}

def parse(path: Path):
    out = {}
    for line in path.read_text(errors="replace").splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        name = m.group(2)
        # Native reflected objects in this Windows x64 dump live in the loaded image
        # around 0x7FF...; transient/Blueprint objects are heap allocations around 0x1xx...
        if addr >= 0x7F0000000000:
            out[name] = addr
    return out

def parse_indices(path: Path):
    out = {}
    for line in path.read_text(errors="replace").splitlines():
        m = INDEX_RE.match(line)
        if m:
            out[m.group(2)] = int(m.group(1), 16)
    return out

if not OLD.exists() or not NEW.exists():
    print("ERROR: both previous property-rich and fresh GObjects dumps are required", file=sys.stderr)
    sys.exit(2)

old = parse(OLD)
new = parse(NEW)
new_indices = parse_indices(NEW)
old_names, new_names = set(old), set(new)
added = new_names - old_names
removed = old_names - new_names
common = old_names & new_names

deltas = collections.Counter(new[name] - old[name] for name in common)

failures = []
if added:
    failures.append(f"{len(added)} module-backed objects were added")
if removed:
    failures.append(f"{len(removed)} module-backed objects were removed")
if len(deltas) != 1:
    failures.append(f"module-backed objects have {len(deltas)} different relocation deltas")
for key in KEYS:
    if key not in old or key not in new:
        failures.append(f"missing key object: {key}")
for key, expected_index in EXPECTED_INDICES.items():
    actual_index = new_indices.get(key)
    if actual_index != expected_index:
        failures.append(
            f"{key} index expected 0x{expected_index:X}, "
            f"fresh={('missing' if actual_index is None else f'0x{actual_index:X}')}"
        )

if failures:
    print("FRESH DUMP LAYOUT CHECK FAILED")
    for failure in failures:
        print(" -", failure)
    if deltas:
        print("Most common deltas:")
        for delta, count in deltas.most_common(10):
            print(f"  {delta:+#x}: {count}")
    sys.exit(1)

delta, count = deltas.most_common(1)[0]
print("PASS: fresh native UObject image layout matches the previous dump")
print(f"Module-backed objects: {len(new):,}")
print(f"Added: {len(added)} | Removed: {len(removed)}")
print(f"Single ASLR relocation delta: {delta:+#x} across {count:,} objects")
print(f"Validated live UClass lookup indices: {len(EXPECTED_INDICES)}")
print("Conclusion: module-relative RVAs/native reflected layout are unchanged between these dumps.")
print(f"Baseline: {OLD}")
print(f"Fresh: {NEW}")
