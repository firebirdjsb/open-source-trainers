#pragma once
#include <cstdint>

// Incursion: Red River / Test_C UE 5.6.1.
//
// Revalidated against the user's 2026-09-01 Dumper-7 raid dump. Its complete
// GObjects-Dump-WithProperties.txt directly confirms the reflected field offsets
// below; tools/verify_dump_offsets.py performs the repeatable check. Dumper-7's
// generated per-class CppSDK/SDK/*.hpp files are still absent, so the property-rich
// object dump remains the authoritative generated layout source.
//
// GWorld/GNames/GObjects are RVAs and MUST be resolved against the EXE module base.
namespace Offsets
{
    // GWorld remains a diagnostic candidate only; the live world is resolved through
    // the validated viewport/GUObjectArray routes because this slot is stale in the
    // current image.
    constexpr uintptr_t GWorld   = 0x9801E98;
    constexpr uintptr_t GNames   = 0x95C67C0;
    // Recovered from the running 2026-09-01 image and structurally validated against
    // five stable UObject indices (668,920 objects, 11 chunks, 0x18 item stride).
    constexpr uintptr_t GObjects = 0xB72B780;

    // Core UObject / UStruct layout used only for validated runtime type checks.
    constexpr uintptr_t UObject_InternalIndex = 0x0C;
    constexpr uintptr_t UObject_ClassPrivate  = 0x10;
    constexpr uintptr_t UObject_NamePrivate   = 0x18;
    constexpr uintptr_t UObject_OuterPrivate  = 0x20;
    constexpr uintptr_t UStruct_SuperStruct   = 0x40;

    constexpr uintptr_t UWorld_PersistentLevel     = 0x30;
    constexpr uintptr_t UWorld_AuthorityGameMode   = 0x1A8;
    constexpr uintptr_t UWorld_GameState           = 0x1B0;
    constexpr uintptr_t UWorld_OwningGameInstance  = 0x228;
    constexpr uintptr_t ULevel_OwningWorld          = 0xC0;
    constexpr uintptr_t UGameInstance_LocalPlayers = 0x38;
    constexpr uintptr_t UPlayer_PlayerController   = 0x30;
    constexpr uintptr_t ULocalPlayer_ViewportClient = 0x78;

    // Alternate live-object routes exposed by the same property-rich dump. These
    // deliberately avoid assuming UWorld::OwningGameInstance is the only route.
    constexpr uintptr_t UEngine_GameViewport              = 0xC10;
    constexpr uintptr_t UGameEngine_GameInstance          = 0x1248;
    constexpr uintptr_t UGameViewportClient_World         = 0x78;
    constexpr uintptr_t UGameViewportClient_GameInstance  = 0x80;

    constexpr uintptr_t AController_PlayerState    = 0x2B0;
    constexpr uintptr_t AController_Pawn           = 0x2E8;
    constexpr uintptr_t AController_ControlRotation = 0x320;

    constexpr uintptr_t APlayerController_Player              = 0x348;
    constexpr uintptr_t APlayerController_AcknowledgedPawn    = 0x350;
    constexpr uintptr_t APlayerController_PlayerCameraManager = 0x360;
    constexpr uintptr_t APlayerController_bIsLocalController  = 0x6C4;

    constexpr uintptr_t AActor_CustomTimeDilation = 0x68;
    constexpr uintptr_t AActor_Owner              = 0x158;
    constexpr uintptr_t AActor_RootComponent       = 0x1B8;
    constexpr uintptr_t AActor_InstanceComponents  = 0x280;
    constexpr uintptr_t AActor_BlueprintCreatedComponents = 0x290;
    constexpr uintptr_t USceneComponent_AttachParent      = 0xC8;
    constexpr uintptr_t USceneComponent_RelativeLocation = 0x140;
    constexpr uintptr_t USceneComponent_RelativeRotation = 0x158;
    constexpr uintptr_t USceneComponent_RelativeScale3D  = 0x170;

    constexpr uintptr_t APawn_PlayerState          = 0x2C8;
    constexpr uintptr_t ACharacter_CharacterMovement = 0x330;
    constexpr uintptr_t ACharacter_CapsuleComponent  = 0x338;
    constexpr uintptr_t UCapsuleComponent_CapsuleHalfHeight = 0x540;
    constexpr uintptr_t UCapsuleComponent_CapsuleRadius     = 0x544;

    // ULevel::Actors is not reflected in this dump. 0xA0 came from the original
    // dumper output/project and is isolated here so it is easy to update.
    constexpr uintptr_t ULevel_Actors = 0xA0;

    constexpr uintptr_t APlayerState_PlayerNamePrivate = 0x340;

    // Test_C.GeneralGameInstance has a native/reflected infinite-ammunition cheat state.
    // This is authoritative game state, unlike WeaponComponent::AmmunitionState which is
    // only a snapshot that the game continuously rebuilds from inventory/magazine data.
    constexpr uintptr_t GeneralGameInstance_bExtendedPartyEnabled = 0x1CA;
    constexpr uintptr_t GeneralGameInstance_bRestock              = 0x220;
    constexpr uintptr_t GeneralGameInstance_bBulletDebugTracesEnabled = 0x24C;
    constexpr uintptr_t GeneralGameInstance_bInfiniteAmmunition = 0x24D;
    constexpr uintptr_t GeneralGameInstance_bOnlyTPSounds          = 0x24E;
    constexpr uintptr_t GeneralGameInstance_bLocalPlayerWeaponSounds = 0x24F;

    constexpr uintptr_t InventoryComponent_FWeightMultiplier = 0x3C0;
    constexpr uintptr_t InventoryComponent_MaxWeight          = 0x3C8;
    constexpr uintptr_t InventoryComponent_MainContainers     = 0x3F8;
    constexpr uintptr_t InventoryComponent_PickUpContainerItem = 0x418;
    constexpr uintptr_t PickUpActor_InventoryComponent         = 0x2C0;
    constexpr uintptr_t InventorySpatialContainerSettings_ContainerSize = 0x38;

    constexpr uintptr_t IRRBaseCharacter_BodyComponent   = 0x660;
    constexpr uintptr_t IRRBaseCharacter_TeamComponent   = 0x670;
    constexpr uintptr_t IRRBaseCharacter_HealthComponent = 0x678;
    constexpr uintptr_t IRRBaseCharacter_WeaponInHands   = 0x680;
    constexpr uintptr_t IRRBaseCharacter_SenseStimulusComponent = 0x6A0;
    constexpr uintptr_t IRRTeamComponent_Hostiles        = 0x118;
    constexpr uintptr_t SenseStimulusBase_bInvisible     = 0xCC;

    // BP_MasterWeapon_C dump. Used only after pointer/plausibility validation.
    constexpr uintptr_t BPMasterWeapon_WeaponComponent = 0x358;
    // Concrete BP weapon subclasses place their EasyBallistics barrel component
    // immediately after BP_MasterWeapon's final reflected field.
    constexpr uintptr_t ConcreteWeapon_BallisticBarrel = 0x368;

    constexpr uintptr_t EBBarrel_MuzzleVelocity = 0x54C;
    constexpr uintptr_t EBBarrel_ChamberedBullet = 0x590;

    constexpr uintptr_t HealthComponent_bEnabled        = 0x1C0;
    constexpr uintptr_t HealthComponent_Health          = 0x1C8;
    constexpr uintptr_t HealthComponent_FallHeightStart = 0x1D0;
    constexpr uintptr_t HealthComponent_bIsInvulnerable = 0x1D4;

    constexpr uintptr_t SimpleGameplayAttribute_BaseData    = 0x30;
    constexpr uintptr_t SimpleGameplayAttribute_CurrentData = 0x3C;
    constexpr uintptr_t SimpleGameplayAttribute_OldData     = 0x48;
    constexpr uintptr_t SimpleAttributeData_BaseValue = 0x0;
    constexpr uintptr_t SimpleAttributeData_MinValue  = 0x4;
    constexpr uintptr_t SimpleAttributeData_MaxValue  = 0x8;
    constexpr uintptr_t SimpleGameplayAttributeHealth_bIsDead = 0x238;

    constexpr uintptr_t CharacterMovement_MaxWalkSpeed         = 0x278;
    constexpr uintptr_t CharacterMovement_MaxWalkSpeedCrouched = 0x27C;
    constexpr uintptr_t AdvancedMovement_MaxSprintSpeed        = 0x1000;
    constexpr uintptr_t AdvancedMovement_MaxSprintSpeedCrouched= 0x1004;
    constexpr uintptr_t AdvancedMovement_MaxSneakSpeed         = 0x1008;
    constexpr uintptr_t AdvancedMovement_MaxSneakSpeedCrouched = 0x100C;
    constexpr uintptr_t AdvancedMovement_MaxProneSpeed         = 0x1010;
    constexpr uintptr_t AdvancedMovement_MaxCheatSpeed         = 0x1014;
    constexpr uintptr_t MovementComponent_Velocity              = 0xD0;
    constexpr uintptr_t CharacterMovement_GravityScale          = 0x1A0;
    constexpr uintptr_t CharacterMovement_MaxStepHeight          = 0x1A4;
    constexpr uintptr_t CharacterMovement_JumpZVelocity          = 0x1A8;
    constexpr uintptr_t CharacterMovement_WalkableFloorAngle     = 0x1CC;
    constexpr uintptr_t CharacterMovement_GroundFriction         = 0x234;
    constexpr uintptr_t CharacterMovement_MaxSwimSpeed           = 0x280;
    constexpr uintptr_t CharacterMovement_MaxCustomMovementSpeed = 0x288;
    constexpr uintptr_t CharacterMovement_MaxAcceleration        = 0x28C;
    constexpr uintptr_t CharacterMovement_BrakingFrictionFactor  = 0x294;
    constexpr uintptr_t CharacterMovement_BrakingDecelerationWalking = 0x2A0;
    constexpr uintptr_t CharacterMovement_AirControl             = 0x2B0;
    constexpr uintptr_t CharacterMovement_MovementMode          = 0x231;
    constexpr uintptr_t CharacterMovement_MaxFlySpeed           = 0x284;
    constexpr uintptr_t CharacterMovement_BrakingDecelerationFlying = 0x2AC;

    constexpr uintptr_t WeaponComponent_AmmunitionState = 0x300;
    constexpr uintptr_t WeaponComponent_DegradationRateSemiMultiplier = 0x1FC;
    constexpr uintptr_t WeaponComponent_DegradationRateAutoMultiplier = 0x200;
    constexpr uintptr_t WeaponComponent_MisfireChance                 = 0x204;
    constexpr uintptr_t WeaponComponent_BlockChance                   = 0x208;
    constexpr uintptr_t AmmunitionState_MagCapacity = 0x8;
    constexpr uintptr_t AmmunitionState_bIsChamberEmpty = 0xC;

    constexpr uintptr_t FirstPersonStamina_RecoverPerSecond = 0x80;
    constexpr uintptr_t FirstPersonStamina_StaminaAttribute = 0xC0;
    constexpr uintptr_t FirstPersonStaminaArm_ArmSwayLocation = 0x128;
    constexpr uintptr_t FirstPersonStaminaArm_ArmSwayRotation = 0x140;

    constexpr uintptr_t FirstPersonWeaponRecoil_TargetCameraRecoil = 0xF8;
    constexpr uintptr_t FirstPersonWeaponRecoil_TargetCameraRecoilCompensation = 0x110;
    constexpr uintptr_t FirstPersonWeaponRecoil_CurrentViewmodelRecoilRotation = 0x128;
    constexpr uintptr_t FirstPersonWeaponRecoil_CurrentViewmodelRecoilLocation = 0x140;
    constexpr uintptr_t FirstPersonWeaponRecoil_CurrentCameraRecoil = 0x1B8;
    constexpr uintptr_t FirstPersonWeaponRecoil_CurrentCameraRecoilCompensation = 0x1D0;
    constexpr uintptr_t FirstPersonWeaponRecoil_FinalViewmodelRecoilRotation = 0x1F0;
    constexpr uintptr_t FirstPersonWeaponRecoil_FinalViewmodelRecoilLocation = 0x208;

    constexpr uintptr_t PlayerCameraManager_CameraCachePrivate = 0x1530;
    constexpr uintptr_t PlayerCameraManager_LastFrameCameraCachePrivate = 0x1E00;
    constexpr uintptr_t CameraCacheEntry_POV = 0x10;
    constexpr uintptr_t MinimalViewInfo_Location = 0x0;
    constexpr uintptr_t MinimalViewInfo_Rotation = 0x18;
    constexpr uintptr_t MinimalViewInfo_FOV      = 0x30;
}

// Stable UObject indices from the fresh Dumper-7 GObjects dump. They are used to
// fetch UClass objects from the live GUObjectArray, not as object addresses. The
// fresh-layout verifier protects this table against future dump drift.
namespace ObjectIndices
{
    constexpr int32_t UFunctionClass             = 0x001F;
    constexpr int32_t UObjectClass              = 0x001E;
    constexpr int32_t ULevelClass               = 0x0443;
    constexpr int32_t UWorldClass               = 0x04B8;
    constexpr int32_t APlayerControllerClass    = 0x076D;
    constexpr int32_t UGameEngineClass          = 0x07F6;
    constexpr int32_t UGameInstanceClass        = 0x0807;
    constexpr int32_t UGameViewportClientClass  = 0x0810;
    constexpr int32_t ULocalPlayerClass          = 0x084C;
    constexpr int32_t SenseStimulusComponentClass = 0x0B01;
    constexpr int32_t IRRBaseCharacterClass      = 0x0FCE;
    constexpr int32_t IRRAIBaseCharacterClass    = 0x0FCF;
    constexpr int32_t GeneralGameInstanceClass   = 0x103C;
    constexpr int32_t EBBarrelClass              = 0x11AE;
    constexpr int32_t IRRBodyComponentClass      = 0x10C3;
    constexpr int32_t FirstPersonStaminaClass    = 0x102B;
    constexpr int32_t FirstPersonStaminaArmClass = 0x102C;
    constexpr int32_t FirstPersonWeaponRecoilClass = 0x1032;
    constexpr int32_t InventoryComponentClass    = 0x1086;
    constexpr int32_t SimpleGameplayAttributeClass = 0x113C;
    constexpr int32_t IRRTeamComponentClass      = 0x10E8;
    constexpr int32_t WeaponComponentClass       = 0x115F;
    constexpr int32_t BPMasterWeaponClass        = 0x11D44;
    constexpr int32_t PickUpActorClass           = 0x111D;
    constexpr int32_t BPAVSDeltaBackpackClass    = 0x1102E;
    constexpr int32_t BackpackStorageSettings0   = 0x1103F;
    constexpr int32_t BackpackStorageSettings1   = 0x11040;

    // Loaded package objects used only as authoritative FName sources for the
    // GeneralGameInstance debug inventory functions. Their names are never guessed.
    constexpr int32_t Package_ID_Bandage         = 0x0C653;
    constexpr int32_t Package_ID_Cash            = 0x0C817;
    constexpr int32_t Package_ID_MarkedCoin      = 0x0C854;
    constexpr int32_t Package_ID_HealthInjector  = 0x0C85C;
    constexpr int32_t DefaultInventoryFunctionLibrary = 0x0907C;
    constexpr int32_t DefaultGameplayStatics     = 0x0874F;
    constexpr int32_t DefaultKismetSystemLibrary = 0x08761;
    constexpr int32_t DefaultGeneralFunctionLibrary = 0x08F1B;
}

// Stable UFunction indices from the same updated Dumper-7 object dump. Every call
// is additionally checked against the live UFunction class and ProcessEvent slot.
namespace FunctionIndices
{
    constexpr int32_t PrimitiveComponent_GetCollisionEnabled = 0x1F5E;
    constexpr int32_t PrimitiveComponent_SetCollisionEnabled = 0x1FA6;
    constexpr int32_t KismetSystemLibrary_LineTraceSingle = 0x1CD2;
    constexpr int32_t Actor_DisableInput = 0x291F;
    constexpr int32_t Actor_EnableInput = 0x2920;
    constexpr int32_t Actor_GetActorEnableCollision = 0x2926;
    constexpr int32_t Actor_GetActorEyesViewPoint = 0x2927;
    constexpr int32_t Actor_IsActorBeingDestroyed = 0x2957;
    constexpr int32_t Actor_SetActorEnableCollision = 0x2994;
    constexpr int32_t Controller_SetControlRotation = 0x29C4;
    constexpr int32_t Controller_SetIgnoreLookInput = 0x29C5;
    constexpr int32_t Controller_SetIgnoreMoveInput = 0x29C6;
    constexpr int32_t Controller_StopMovement = 0x29C8;
    constexpr int32_t EBBarrel_CalculateAimDirectionFromLocation = 0x2B75;
    constexpr int32_t PlayerController_ProjectWorldLocationToScreen = 0x2A36;
    constexpr int32_t WeaponComponent_GetDurabilityPercentage = 0x2B6F;
    constexpr int32_t CharacterMovementComponent_SetMovementMode = 0x527E;
    constexpr int32_t GameplayStatics_BeginDeferredActorSpawnFromClass = 0x54F0;
    constexpr int32_t GameplayStatics_FinishSpawningActor = 0x5507;
    constexpr int32_t SenseStimulusComponent_SetInvisibleServer = 0x5FC4;
    constexpr int32_t Actor_K2_SetActorLocation = 0x296F;
    constexpr int32_t Actor_K2_SetActorLocationAndRotation = 0x2970;
    constexpr int32_t Controller_GetPlayerViewPoint = 0x29B2;
    constexpr int32_t Controller_LineOfSightTo = 0x29BA;
    constexpr int32_t GeneralGameInstance_CompleteTrackedMissions = 0x6B70;
    constexpr int32_t GeneralGameInstance_FailTrackedMissions = 0x6B74;
    constexpr int32_t GeneralGameInstance_ForceExtraction = 0x6B76;
    constexpr int32_t GeneralGameInstance_KillAllAI = 0x6B80;
    constexpr int32_t GeneralGameInstance_RegenerateMissions = 0x6B84;
    constexpr int32_t GeneralGameInstance_ResetAchievements = 0x6B86;
    constexpr int32_t GeneralGameInstance_ResetInventory = 0x6B87;
    constexpr int32_t GeneralGameInstance_ResetMissionSystem = 0x6B88;
    constexpr int32_t GeneralGameInstance_ResetRaidData = 0x6B89;
    constexpr int32_t GeneralGameInstance_ResetResources = 0x6B8A;
    constexpr int32_t GeneralGameInstance_ResetTrackedMissions = 0x6B8C;
    constexpr int32_t GeneralGameInstance_ResetVendor = 0x6B8D;
    constexpr int32_t GeneralGameInstance_ResetWorld = 0x6B8E;
    constexpr int32_t GeneralGameInstance_SetAIOptimizationLevel = 0x6B8F;
    constexpr int32_t GeneralGameInstance_SetDurability = 0x6B90;
    constexpr int32_t GeneralGameInstance_SetRestock = 0x6B96;
    constexpr int32_t GeneralGameInstance_SetSuccessRate = 0x6B97;
    constexpr int32_t GeneralGameInstance_SetTimeOfDay = 0x6B98;
    constexpr int32_t GeneralGameInstance_SetWeatherType = 0x6B9A;
    constexpr int32_t GeneralGameInstance_ShowAllAI = 0x6B9B;
    constexpr int32_t GeneralGameInstance_ToggleAIOptimization = 0x6BA2;
    constexpr int32_t GeneralGameInstance_ToggleCheatMovementSpeed = 0x6BA4;
    constexpr int32_t GeneralGameInstance_ToggleCinematicCamera = 0x6BA5;
    constexpr int32_t GeneralGameInstance_ToggleDebugOperations = 0x6BA6;
    constexpr int32_t GeneralGameInstance_ToggleExtendedParty = 0x6BA7;
    constexpr int32_t GeneralGameInstance_ToggleLocalPlayerWeaponSounds = 0x6BAB;
    constexpr int32_t GeneralGameInstance_ToggleTPSoundsOnly = 0x6BAC;
    constexpr int32_t GeneralGameInstance_ToggleBulletDebugTraces = 0x6BA3;
    constexpr int32_t GeneralGameInstance_ToggleInfiniteAmmunition = 0x6BA8;
    constexpr int32_t GeneralGameInstance_ToggleInvisible = 0x6BA9;
    constexpr int32_t GeneralGameInstance_ToggleInvulnerable = 0x6BAA;
    constexpr int32_t GeneralGameInstance_AddItem = 0x6B6E;
    constexpr int32_t GeneralGameInstance_AddResource = 0x6B6F;
    constexpr int32_t GeneralGameInstance_SetFactionReputation = 0x6B91;
    constexpr int32_t GeneralGameInstance_SetLevel = 0x6B93;
    constexpr int32_t GeneralGameInstance_SetLumenScalability = 0x6B94;
    constexpr int32_t GeneralGameInstance_SetOverallFactionReputation = 0x6B95;
    constexpr int32_t FirstPersonStamina_GetCurrentStamina = 0x6B0D;
    constexpr int32_t InventoryComponent_AddDefaultItem = 0x6C1A;
    constexpr int32_t InventoryComponent_SpawnItem = 0x6CA0;
    constexpr int32_t InventoryComponent_CanAddToInventory = 0x6C2B;
    constexpr int32_t InventoryComponent_TryAddItem = 0x6CA3;
    constexpr int32_t InventoryComponent_ItemsUpdated = 0x6C78;
    constexpr int32_t InventoryComponent_OnRepMainContainers = 0x6C80;
    constexpr int32_t InventoryComponent_UpdateCurrency = 0x6CB3;
    constexpr int32_t InventoryComponent_SetInfiniteAmmunition = 0x6C9E;
    constexpr int32_t InventoryComponent_GetInfiniteAmmunition = 0x6C55;
    constexpr int32_t InventoryComponent_ToggleInfiniteAmmunition = 0x6CA1;
    constexpr int32_t SimpleGameplayAttribute_GetCurrentMaxValue = 0x6EEF;
    constexpr int32_t SimpleGameplayAttribute_GetCurrentValue = 0x6EF1;
    constexpr int32_t SimpleGameplayAttribute_SetBaseValue = 0x6EF7;
    constexpr int32_t InventoryFunctionLibrary_AddItemByRowName = 0x6F99;
    constexpr int32_t InventoryFunctionLibrary_GetPlayerInventoryComponent = 0x6FBA;
    constexpr int32_t InventoryFunctionLibrary_GetStashInventoryComponent = 0x6FBD;
    constexpr int32_t IRRItemPreset_GetDefaultItem = 0x6DEE;
    constexpr int32_t IRRBodyComponent_GetBodyPartLocation = 0x6DBF;
    constexpr int32_t IRRBodyComponent_GetEyeLocation = 0x6DC0;
    constexpr int32_t IRRBodyComponent_GetMainBoneLocations = 0x6DC1;
    constexpr int32_t GeneralFunctionLibrary_CheckSphereVisibility = 0x6F56;
}
