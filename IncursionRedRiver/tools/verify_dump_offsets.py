#!/usr/bin/env python3
from pathlib import Path
import argparse
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DUMP_DIR = ROOT / "IncursionCheat_DX12" / "5.6.1-0+UE5-Test_C"

parser = argparse.ArgumentParser(
    description="Verify reflected runtime offsets against a Dumper-7 property dump."
)
parser.add_argument(
    "--dump-dir",
    type=Path,
    default=DEFAULT_DUMP_DIR,
    help="Directory containing GObjects-Dump-WithProperties.txt",
)
args = parser.parse_args()
DUMP = args.dump_dir / "GObjects-Dump-WithProperties.txt"

# Reflected properties used by the repaired runtime. ULevel::Actors and the global
# addresses are intentionally excluded because they are native/global values, not UPROPERTY data.
EXPECTED = [
    ("Class Engine.World", "PersistentLevel", 0x30),
    ("Class Engine.World", "AuthorityGameMode", 0x1A8),
    ("Class Engine.World", "GameState", 0x1B0),
    ("Class Engine.World", "OwningGameInstance", 0x228),
    ("Class Engine.Level", "OwningWorld", 0xC0),
    ("Class Engine.GameInstance", "LocalPlayers", 0x38),
    ("Class Engine.Player", "PlayerController", 0x30),
    ("Class Engine.LocalPlayer", "ViewportClient", 0x78),
    ("Class Engine.Engine", "GameViewport", 0xC10),
    ("Class Engine.GameEngine", "GameInstance", 0x1248),
    ("Class Engine.GameViewportClient", "World", 0x78),
    ("Class Engine.GameViewportClient", "GameInstance", 0x80),
    ("Class Engine.Controller", "Pawn", 0x2E8),
    ("Class Engine.Controller", "ControlRotation", 0x320),
    ("Class Engine.PlayerController", "Player", 0x348),
    ("Class Engine.PlayerController", "AcknowledgedPawn", 0x350),
    ("Class Engine.PlayerController", "PlayerCameraManager", 0x360),
    ("Class Engine.PlayerController", "bIsLocalPlayerController", 0x6C4),
    ("Class Engine.Actor", "CustomTimeDilation", 0x68),
    ("Class Engine.Actor", "Owner", 0x158),
    ("Class Engine.Actor", "RootComponent", 0x1B8),
    ("Class Engine.Actor", "InstanceComponents", 0x280),
    ("Class Engine.Actor", "BlueprintCreatedComponents", 0x290),
    ("Class Engine.SceneComponent", "AttachParent", 0xC8),
    ("Class Engine.SceneComponent", "RelativeLocation", 0x140),
    ("Class Engine.SceneComponent", "RelativeRotation", 0x158),
    ("Class Engine.SceneComponent", "RelativeScale3D", 0x170),
    ("Class Engine.Pawn", "PlayerState", 0x2C8),
    ("Class Engine.Character", "Mesh", 0x328),
    ("Class Engine.SkinnedMeshComponent", "SkeletalMesh", 0x578),
    ("Class Engine.SkinnedMeshComponent", "SkinnedAsset", 0x580),
    ("Class Engine.SkeletalMesh", "Skeleton", 0xF8),
    ("Class Engine.Character", "CharacterMovement", 0x330),
    ("Class Engine.SkeletalMeshComponent", "CachedComponentSpaceTransforms", 0x9B8),
    ("Class Engine.Character", "CapsuleComponent", 0x338),
    ("Class Engine.CapsuleComponent", "CapsuleHalfHeight", 0x540),
    ("Class Engine.CapsuleComponent", "CapsuleRadius", 0x544),
    ("Class Engine.PlayerState", "PlayerNamePrivate", 0x340),
    ("Class Test_C.GeneralGameInstance", "bExtendedPartyEnabled", 0x1CA),
    ("Class Test_C.GeneralGameInstance", "bRestock", 0x220),
    ("Class Test_C.GeneralGameInstance", "bBulletDebugTracesEnabled", 0x24C),
    ("Class Test_C.GeneralGameInstance", "bInfiniteAmmunition", 0x24D),
    ("Class Test_C.GeneralGameInstance", "bOnlyTPSounds", 0x24E),
    ("Class Test_C.GeneralGameInstance", "bLocalPlayerWeaponSounds", 0x24F),
    ("Class Test_C.InventoryComponent", "FWeightMultiplier", 0x3C0),
    ("Class Test_C.InventoryComponent", "MaxWeight", 0x3C8),
    ("Class Test_C.InventoryComponent", "MainContainers", 0x3F8),
    ("Class Test_C.IRRBaseCharacter", "TeamComponent", 0x670),
    ("Class Test_C.IRRBaseCharacter", "BodyComponent", 0x660),
    ("Class Test_C.IRRBaseCharacter", "HealthComponent", 0x678),
    ("Class Test_C.IRRBaseCharacter", "WeaponInHands", 0x680),
    ("Class Test_C.IRRBaseCharacter", "SenseStimulusComponent", 0x6A0),
    ("Class Test_C.IRRTeamComponent", "Hostiles", 0x118),
    ("Class SenseSystem.SenseStimulusBase", "bInvisible", 0xCC),
    ("Class Test_C.HealthComponent", "bEnabled", 0x1C0),
    ("Class Test_C.HealthComponent", "Health", 0x1C8),
    ("Class Test_C.HealthComponent", "FallHeightStart", 0x1D0),
    ("Class Test_C.HealthComponent", "bIsInvulnerable", 0x1D4),
    ("Class Test_C.SimpleGameplayAttribute", "BaseData", 0x30),
    ("Class Test_C.SimpleGameplayAttribute", "CurrentData", 0x3C),
    ("Class Test_C.SimpleGameplayAttribute", "OldData", 0x48),
    ("ScriptStruct Test_C.SimpleAttributeData", "BaseValue", 0x0),
    ("ScriptStruct Test_C.SimpleAttributeData", "MinValue", 0x4),
    ("ScriptStruct Test_C.SimpleAttributeData", "MaxValue", 0x8),
    ("Class Test_C.SimpleGameplayAttribute_Health", "bIsDead", 0x238),
    ("Class Engine.CharacterMovementComponent", "MaxWalkSpeed", 0x278),
    ("Class Engine.CharacterMovementComponent", "MaxWalkSpeedCrouched", 0x27C),
    ("Class Engine.MovementComponent", "Velocity", 0xD0),
    ("Class Engine.CharacterMovementComponent", "GravityScale", 0x1A0),
    ("Class Engine.CharacterMovementComponent", "MaxStepHeight", 0x1A4),
    ("Class Engine.CharacterMovementComponent", "JumpZVelocity", 0x1A8),
    ("Class Engine.CharacterMovementComponent", "WalkableFloorAngle", 0x1CC),
    ("Class Engine.CharacterMovementComponent", "GroundFriction", 0x234),
    ("Class Engine.CharacterMovementComponent", "MaxSwimSpeed", 0x280),
    ("Class Engine.CharacterMovementComponent", "MaxCustomMovementSpeed", 0x288),
    ("Class Engine.CharacterMovementComponent", "MaxAcceleration", 0x28C),
    ("Class Engine.CharacterMovementComponent", "BrakingFrictionFactor", 0x294),
    ("Class Engine.CharacterMovementComponent", "BrakingDecelerationWalking", 0x2A0),
    ("Class Engine.CharacterMovementComponent", "AirControl", 0x2B0),
    ("Class Engine.CharacterMovementComponent", "MovementMode", 0x231),
    ("Class Engine.CharacterMovementComponent", "MaxFlySpeed", 0x284),
    ("Class Engine.CharacterMovementComponent", "BrakingDecelerationFlying", 0x2AC),
    ("Class Test_C.AdvancedMovementComponent", "MaxSprintSpeed", 0x1000),
    ("Class Test_C.AdvancedMovementComponent", "MaxSprintSpeedCrouched", 0x1004),
    ("Class Test_C.AdvancedMovementComponent", "MaxSneakSpeed", 0x1008),
    ("Class Test_C.AdvancedMovementComponent", "MaxSneakSpeedCrouched", 0x100C),
    ("Class Test_C.AdvancedMovementComponent", "MaxProneSpeed", 0x1010),
    ("Class Test_C.AdvancedMovementComponent", "MaxCheatSpeed", 0x1014),
    ("BlueprintGeneratedClass BP_MasterWeapon.BP_MasterWeapon_C", "BP_WeaponComponent", 0x358),
    ("Class EasyBallistics.EBBarrel", "MuzzleVelocity", 0x54C),
    ("Class EasyBallistics.EBBarrel", "ChamberedBullet", 0x590),
    ("Class Test_C.WeaponComponent", "AmmunitionState", 0x300),
    ("Class Test_C.WeaponComponent", "DegradationRate_SemiMultiplier", 0x1FC),
    ("Class Test_C.WeaponComponent", "DegradationRate_AutoMultiplier", 0x200),
    ("Class Test_C.WeaponComponent", "MisfireChance", 0x204),
    ("Class Test_C.WeaponComponent", "BlockChance", 0x208),
    ("ScriptStruct Test_C.AmmunitionState", "MagCapacity", 0x8),
    ("ScriptStruct Test_C.AmmunitionState", "bIsChamberEmpty", 0xC),
    ("Class Test_C.FirstPersonStamina", "RecoverPerSecond", 0x80),
    ("Class Test_C.FirstPersonStamina", "StaminaAttribute", 0xC0),
    ("Class Test_C.FirstPersonStamina_Arm", "ArmSwayLocation", 0x128),
    ("Class Test_C.FirstPersonStamina_Arm", "ArmSwayRotation", 0x140),
    ("Class Test_C.FirstPersonWeaponRecoil", "TargetCameraRecoil", 0xF8),
    ("Class Test_C.FirstPersonWeaponRecoil", "TargetCameraRecoilCompensation", 0x110),
    ("Class Test_C.FirstPersonWeaponRecoil", "CurrentViewmodelRecoilRotation", 0x128),
    ("Class Test_C.FirstPersonWeaponRecoil", "CurrentViewmodelRecoilLocation", 0x140),
    ("Class Test_C.FirstPersonWeaponRecoil", "CurrentCameraRecoil", 0x1B8),
    ("Class Test_C.FirstPersonWeaponRecoil", "CurrentCameraRecoilCompensation", 0x1D0),
    ("Class Test_C.FirstPersonWeaponRecoil", "FinalViewmodelRecoilRotation", 0x1F0),
    ("Class Test_C.FirstPersonWeaponRecoil", "FinalViewmodelRecoilLocation", 0x208),
    ("Class Engine.PlayerCameraManager", "CameraCachePrivate", 0x1530),
    ("Class Engine.PlayerCameraManager", "LastFrameCameraCachePrivate", 0x1E00),
    ("ScriptStruct Engine.CameraCacheEntry", "POV", 0x10),
    ("ScriptStruct Engine.MinimalViewInfo", "Location", 0x0),
    ("ScriptStruct Engine.MinimalViewInfo", "Rotation", 0x18),
    ("ScriptStruct Engine.MinimalViewInfo", "FOV", 0x30),
]

HEADER_RE = re.compile(r"^\[[0-9A-Fa-f]{8}\]\s+\{0x[0-9A-Fa-f]+\}\s+(Class|ScriptStruct|BlueprintGeneratedClass|Function)\s+(.+)$")
PROP_RE = re.compile(r"^\[([0-9A-Fa-f]{8})\]\s+\{0x[0-9A-Fa-f]+\}\s+\s+\w+Property\s+(.+?)\??$")

if not DUMP.exists():
    print(f"ERROR: missing {DUMP}", file=sys.stderr)
    sys.exit(2)

lines = DUMP.read_text(errors="replace").splitlines()

# Build exact top-level object header ranges. This avoids the old substring bug where
# "Class Engine.World" accidentally matched "Class Engine.WorldSubsystem" first.
headers = []
for i, line in enumerate(lines):
    m = HEADER_RE.match(line)
    if m:
        headers.append((i, f"{m.group(1)} {m.group(2)}"))
owner_to_range = {}
for idx, (start, name) in enumerate(headers):
    end = headers[idx + 1][0] if idx + 1 < len(headers) else len(lines)
    owner_to_range.setdefault(name, (start, end))

failures = []
for owner, prop, expected in EXPECTED:
    r = owner_to_range.get(owner)
    if not r:
        failures.append(f"missing owner: {owner}")
        continue
    start, end = r
    found = None
    for line in lines[start + 1:end]:
        m = PROP_RE.match(line)
        if not m:
            continue
        prop_name = m.group(2)
        if prop_name == prop:
            found = int(m.group(1), 16)
            break
    if found != expected:
        failures.append(
            f"{owner}::{prop}: expected 0x{expected:X}, "
            f"dump={('missing' if found is None else f'0x{found:X}')}"
        )

if failures:
    print("OFFSET VERIFICATION FAILED")
    for failure in failures:
        print(" -", failure)
    sys.exit(1)

print(f"PASS: verified {len(EXPECTED)} reflected offsets against {DUMP}")
print("NOTE: global RVAs and ULevel::Actors are not UPROPERTY entries; run verify_fresh_dump_layout.py for current-build native-layout validation.")
