#include "UECheats.h"

#include "../Memory/Memory.h"
#include "../sdk/GameAccess.h"
#include "../sdk/Offsets.h"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{
    struct MovementBackup
    {
        uintptr_t Component = 0;
        std::array<float, 8> Values{};
        bool Valid = false;
    };

    MovementBackup g_movementBackup{};

    struct GodModeBackup
    {
        uintptr_t Component = 0;
        bool Invulnerable = false;
        bool Valid = false;
    };

    GodModeBackup g_godModeBackup{};

    struct InfiniteAmmoBackup
    {
        uintptr_t GameInstance = 0;
        bool OriginalValue = false;
        uintptr_t Inventory = 0;
        bool InventoryOriginalValue = false;
        bool InventoryOriginalValid = false;
        bool Valid = false;
    };

    InfiniteAmmoBackup g_infiniteAmmoBackup{};
    uintptr_t g_ammoAppliedGameInstance = 0;
    uintptr_t g_ammoAppliedInventory = 0;
    bool g_ammoFunctionState = false;
    bool g_ammoFunctionStateValid = false;

    struct InvisibleBackup
    {
        uintptr_t Component = 0;
        bool OriginalValue = false;
        bool Valid = false;
    };

    InvisibleBackup g_invisibleBackup{};
    uintptr_t g_invisibleAppliedComponent = 0;

    struct FlyBackup
    {
        uintptr_t Component = 0;
        float GravityScale = 0.0f;
        float MaxFlySpeed = 0.0f;
        float BrakingDeceleration = 0.0f;
        uint8_t MovementMode = 0;
        bool Valid = false;
    };

    FlyBackup g_flyBackup{};
    uintptr_t g_flyModeAppliedComponent = 0;

    ULONGLONG g_staminaLastFunctionApply = 0;
    bool g_staminaFunctionDispatch = false;
    float g_staminaCurrent = 0.0f;
    float g_staminaMaximum = 0.0f;
    int g_staminaValidatedChannels = 0;
    int g_staminaFullChannels = 0;
    char g_staminaStatus[128] = "Safe reflected path idle";

    struct NoClipBackup
    {
        uintptr_t Pawn = 0;
        uintptr_t Capsule = 0;
        bool ActorCollision = true;
        uint8_t CapsuleCollisionMode = 3;
        bool ActorValueValid = false;
        bool CapsuleValueValid = false;
        bool Valid = false;
    };

    NoClipBackup g_noClipBackup{};
    uintptr_t g_noClipAppliedPawn = 0;
    ULONGLONG g_noClipLastApply = 0;

    struct FloatBackup
    {
        uintptr_t Object = 0;
        float Value = 0.0f;
        bool Valid = false;
    };

    struct WeaponConditionBackup
    {
        uintptr_t Component = 0;
        std::array<float, 4> Values{};
        bool Valid = false;
    };

    FloatBackup g_weightBackup{};
    WeaponConditionBackup g_weaponConditionBackup{};
    uintptr_t g_invisibleAppliedInstance = 0;
    uintptr_t g_bulletTraceAppliedInstance = 0;
    uintptr_t g_invulnerableAppliedInstance = 0;

    void ApplyGameInstanceToggle(bool enabled, int32_t functionIndex,
                                 uintptr_t& appliedInstance);

    constexpr std::array<uintptr_t, 8> SpeedOffsets = {
        Offsets::CharacterMovement_MaxWalkSpeed,
        Offsets::CharacterMovement_MaxWalkSpeedCrouched,
        Offsets::AdvancedMovement_MaxSprintSpeed,
        Offsets::AdvancedMovement_MaxSprintSpeedCrouched,
        Offsets::AdvancedMovement_MaxSneakSpeed,
        Offsets::AdvancedMovement_MaxSneakSpeedCrouched,
        Offsets::AdvancedMovement_MaxProneSpeed,
        Offsets::AdvancedMovement_MaxCheatSpeed
    };

    uintptr_t GetMovementComponent()
    {
        const uintptr_t pawn = GameAccess::GetLocalPawn();
        return pawn ? Memory::Read<uintptr_t>(pawn + Offsets::ACharacter_CharacterMovement) : 0;
    }

    void RestoreSpeed()
    {
        if (!g_movementBackup.Valid || !g_movementBackup.Component)
            return;
        for (size_t i = 0; i < SpeedOffsets.size(); ++i)
            Memory::Write(g_movementBackup.Component + SpeedOffsets[i], g_movementBackup.Values[i]);
        g_movementBackup = {};
    }

    bool CaptureSpeed(uintptr_t movement)
    {
        if (!movement)
            return false;
        MovementBackup backup{};
        backup.Component = movement;
        for (size_t i = 0; i < SpeedOffsets.size(); ++i)
        {
            const float value = Memory::Read<float>(movement + SpeedOffsets[i]);
            if (!(value >= 0.0f && value < 100000.0f))
                return false;
            backup.Values[i] = value;
        }
        backup.Valid = true;
        g_movementBackup = backup;
        return true;
    }

    void ApplySpeed(float multiplier)
    {
        const uintptr_t movement = GetMovementComponent();
        if (!movement)
            return;
        if (!g_movementBackup.Valid || g_movementBackup.Component != movement)
        {
            RestoreSpeed();
            if (!CaptureSpeed(movement))
                return;
        }

        multiplier = std::clamp(multiplier, 0.25f, 5.0f);
        for (size_t i = 0; i < SpeedOffsets.size(); ++i)
            Memory::Write(movement + SpeedOffsets[i], g_movementBackup.Values[i] * multiplier);
    }

    void RestoreFly()
    {
        if (g_flyBackup.Valid && g_flyBackup.Component)
        {
            alignas(8) uint8_t movementParams[8]{};
            movementParams[0] = g_flyBackup.MovementMode;
            GameAccess::InvokeFunctionRaw(g_flyBackup.Component,
                FunctionIndices::CharacterMovementComponent_SetMovementMode,
                movementParams, sizeof(movementParams));
            Memory::Write(g_flyBackup.Component + Offsets::CharacterMovement_GravityScale,
                          g_flyBackup.GravityScale);
            Memory::Write(g_flyBackup.Component + Offsets::CharacterMovement_MaxFlySpeed,
                          g_flyBackup.MaxFlySpeed);
            Memory::Write(g_flyBackup.Component + Offsets::CharacterMovement_BrakingDecelerationFlying,
                          g_flyBackup.BrakingDeceleration);
            Memory::Write(g_flyBackup.Component + Offsets::CharacterMovement_MovementMode,
                          g_flyBackup.MovementMode);
        }
        g_flyBackup = {};
        g_flyModeAppliedComponent = 0;
    }

    bool CaptureFly(uintptr_t movement)
    {
        FlyBackup backup{};
        backup.Component = movement;
        if (!Memory::TryRead(movement + Offsets::CharacterMovement_GravityScale,
                            backup.GravityScale) ||
            !Memory::TryRead(movement + Offsets::CharacterMovement_MaxFlySpeed,
                            backup.MaxFlySpeed) ||
            !Memory::TryRead(movement + Offsets::CharacterMovement_BrakingDecelerationFlying,
                            backup.BrakingDeceleration) ||
            !Memory::TryRead(movement + Offsets::CharacterMovement_MovementMode,
                            backup.MovementMode) ||
            !std::isfinite(backup.GravityScale) ||
            !std::isfinite(backup.MaxFlySpeed) ||
            !std::isfinite(backup.BrakingDeceleration) ||
            std::abs(backup.GravityScale) > 100.0f ||
            backup.MaxFlySpeed < 0.0f || backup.MaxFlySpeed > 100000.0f ||
            backup.BrakingDeceleration < 0.0f || backup.BrakingDeceleration > 100000.0f ||
            backup.MovementMode > 6)
            return false;
        backup.Valid = true;
        g_flyBackup = backup;
        return true;
    }

    void ApplyFly(float speed)
    {
        const uintptr_t movement = GetMovementComponent();
        if (!movement)
            return;
        if (!g_flyBackup.Valid || g_flyBackup.Component != movement)
        {
            RestoreFly();
            if (!CaptureFly(movement))
                return;
        }

        speed = std::clamp(speed, 100.0f, 5000.0f);
        constexpr float noGravity = 0.0f;
        constexpr uint8_t flyingMode = 5; // EMovementMode::MOVE_Flying
        const float braking = std::max(speed * 4.0f, 2048.0f);
        if (g_flyModeAppliedComponent != movement)
        {
            alignas(8) uint8_t movementParams[8]{};
            movementParams[0] = flyingMode;
            if (GameAccess::InvokeFunctionRaw(movement,
                    FunctionIndices::CharacterMovementComponent_SetMovementMode,
                    movementParams, sizeof(movementParams)))
                g_flyModeAppliedComponent = movement;
        }
        Memory::Write(movement + Offsets::CharacterMovement_GravityScale, noGravity);
        Memory::Write(movement + Offsets::CharacterMovement_MaxFlySpeed, speed);
        Memory::Write(movement + Offsets::CharacterMovement_BrakingDecelerationFlying, braking);
        Memory::Write(movement + Offsets::CharacterMovement_MovementMode, flyingMode);

        FVector velocity{};
        if (Memory::TryRead(movement + Offsets::MovementComponent_Velocity, velocity) &&
            velocity.IsFinite())
        {
            const bool ascend = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
            const bool descend = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            velocity.Z = ascend == descend ? 0.0 : (ascend ? speed : -speed);
            Memory::Write(movement + Offsets::MovementComponent_Velocity, velocity);
        }
    }

    void SetCollisionMode(uintptr_t component, uint8_t mode)
    {
        alignas(8) uint8_t params[8]{};
        params[0] = mode;
        GameAccess::InvokeFunctionRaw(component,
            FunctionIndices::PrimitiveComponent_SetCollisionEnabled,
            params, sizeof(params));
    }

    void RestoreNoClip()
    {
        if (g_noClipBackup.Valid)
        {
            if (g_noClipBackup.Pawn && g_noClipBackup.ActorValueValid)
                GameAccess::InvokeBooleanFunction(g_noClipBackup.Pawn,
                    FunctionIndices::Actor_SetActorEnableCollision,
                    g_noClipBackup.ActorCollision);
            if (g_noClipBackup.Capsule && g_noClipBackup.CapsuleValueValid)
                SetCollisionMode(g_noClipBackup.Capsule,
                                 g_noClipBackup.CapsuleCollisionMode);
        }
        g_noClipBackup = {};
        g_noClipAppliedPawn = 0;
        g_noClipLastApply = 0;
    }

    bool CaptureNoClip(uintptr_t pawn, uintptr_t capsule)
    {
        NoClipBackup backup{};
        backup.Pawn = pawn;
        backup.Capsule = capsule;
        backup.ActorValueValid = GameAccess::QueryBooleanFunction(pawn,
            FunctionIndices::Actor_GetActorEnableCollision,
            backup.ActorCollision);
        backup.CapsuleValueValid = GameAccess::QueryByteFunction(capsule,
            FunctionIndices::PrimitiveComponent_GetCollisionEnabled,
            backup.CapsuleCollisionMode);
        backup.Valid = backup.ActorValueValid || backup.CapsuleValueValid;
        if (!backup.Valid)
            return false;
        g_noClipBackup = backup;
        return true;
    }

    void ApplyNoClip()
    {
        const uintptr_t pawn = GameAccess::GetLocalPawn();
        const uintptr_t capsule = pawn ? Memory::Read<uintptr_t>(pawn +
            Offsets::ACharacter_CapsuleComponent) : 0;
        if (!pawn || !capsule)
            return;
        if (!g_noClipBackup.Valid || g_noClipBackup.Pawn != pawn ||
            g_noClipBackup.Capsule != capsule)
        {
            RestoreNoClip();
            if (!CaptureNoClip(pawn, capsule))
                return;
        }

        // Disable both the actor-wide collision gate and the character capsule's
        // collision mode. Noclip intentionally shares the already-validated flying
        // movement path so gravity cannot push the pawn into geometry.
        const ULONGLONG now = GetTickCount64();
        if (g_noClipAppliedPawn != pawn || now - g_noClipLastApply >= 1000)
        {
            GameAccess::InvokeBooleanFunction(pawn,
                FunctionIndices::Actor_SetActorEnableCollision, false);
            SetCollisionMode(capsule, 0); // ECollisionEnabled::NoCollision
            g_noClipAppliedPawn = pawn;
            g_noClipLastApply = now;
        }
        ApplyFly(UECheats::flySpeed);
    }

    uintptr_t GetWeaponComponent()
    {
        return GameAccess::GetWeaponComponent();
    }

    void ApplyInfiniteAmmo()
    {
        const auto& diagnostics = GameAccess::GetDiagnostics();
        const uintptr_t gameInstance = GameAccess::GetGameInstance();
        if (!gameInstance || !diagnostics.GameInstanceTypeValid ||
            !diagnostics.PlayerController || !diagnostics.Pawn)
            return;

        if (!g_infiniteAmmoBackup.Valid || g_infiniteAmmoBackup.GameInstance != gameInstance)
        {
            g_infiniteAmmoBackup.GameInstance = gameInstance;
            g_infiniteAmmoBackup.OriginalValue = Memory::Read<bool>(
                gameInstance + Offsets::GeneralGameInstance_bInfiniteAmmunition);
            g_infiniteAmmoBackup.Valid = true;
        }

        const bool enabled = true;
        Memory::Write(gameInstance + Offsets::GeneralGameInstance_bInfiniteAmmunition, enabled);

        const uintptr_t inventory = GameAccess::GetInventoryComponent();
        if (inventory && (!g_infiniteAmmoBackup.InventoryOriginalValid ||
                          g_infiniteAmmoBackup.Inventory != inventory))
        {
            bool original = false;
            if (GameAccess::QueryBooleanFunction(inventory,
                    FunctionIndices::InventoryComponent_GetInfiniteAmmunition,
                    original))
            {
                g_infiniteAmmoBackup.Inventory = inventory;
                g_infiniteAmmoBackup.InventoryOriginalValue = original;
                g_infiniteAmmoBackup.InventoryOriginalValid = true;
            }
        }
        if (g_ammoAppliedGameInstance != gameInstance)
        {
            if (GameAccess::InvokeBooleanFunction(gameInstance,
                    FunctionIndices::GeneralGameInstance_ToggleInfiniteAmmunition, true))
                g_ammoAppliedGameInstance = gameInstance;
        }
        if (inventory && g_ammoAppliedInventory != inventory)
        {
            bool dispatched = GameAccess::InvokeBooleanFunction(inventory,
                FunctionIndices::InventoryComponent_SetInfiniteAmmunition, true);
            bool reported = false;
            g_ammoFunctionStateValid = GameAccess::QueryBooleanFunction(inventory,
                FunctionIndices::InventoryComponent_GetInfiniteAmmunition, reported);
            g_ammoFunctionState = reported;
            if (g_ammoFunctionStateValid && !reported)
            {
                dispatched = GameAccess::InvokeBooleanFunction(inventory,
                    FunctionIndices::InventoryComponent_ToggleInfiniteAmmunition, true) || dispatched;
                g_ammoFunctionStateValid = GameAccess::QueryBooleanFunction(inventory,
                    FunctionIndices::InventoryComponent_GetInfiniteAmmunition, reported);
                g_ammoFunctionState = reported;
            }
            if (dispatched)
                g_ammoAppliedInventory = inventory;
        }
    }

    void RestoreInfiniteAmmo()
    {
        if (g_infiniteAmmoBackup.Valid && g_infiniteAmmoBackup.GameInstance)
        {
            GameAccess::InvokeBooleanFunction(g_infiniteAmmoBackup.GameInstance,
                FunctionIndices::GeneralGameInstance_ToggleInfiniteAmmunition,
                g_infiniteAmmoBackup.OriginalValue);
            const uintptr_t inventory = g_infiniteAmmoBackup.InventoryOriginalValid ?
                g_infiniteAmmoBackup.Inventory : GameAccess::GetInventoryComponent();
            const bool inventoryOriginal = g_infiniteAmmoBackup.InventoryOriginalValid ?
                g_infiniteAmmoBackup.InventoryOriginalValue : g_infiniteAmmoBackup.OriginalValue;
            if (inventory)
            {
                GameAccess::InvokeBooleanFunction(inventory,
                    FunctionIndices::InventoryComponent_SetInfiniteAmmunition,
                    inventoryOriginal);
                bool reported = false;
                if (GameAccess::QueryBooleanFunction(inventory,
                        FunctionIndices::InventoryComponent_GetInfiniteAmmunition, reported) &&
                    reported != inventoryOriginal)
                    GameAccess::InvokeBooleanFunction(inventory,
                        FunctionIndices::InventoryComponent_ToggleInfiniteAmmunition,
                        inventoryOriginal);
            }
            Memory::Write(g_infiniteAmmoBackup.GameInstance + Offsets::GeneralGameInstance_bInfiniteAmmunition,
                          g_infiniteAmmoBackup.OriginalValue);
        }
        g_infiniteAmmoBackup = {};
        g_ammoAppliedGameInstance = 0;
        g_ammoAppliedInventory = 0;
        g_ammoFunctionState = false;
        g_ammoFunctionStateValid = false;
    }

    void ApplyGodMode()
    {
        const uintptr_t pawn = GameAccess::GetLocalPawn();
        if (!pawn)
            return;
        const auto health = GameAccess::GetHealth(pawn);
        if (!health.Valid || !health.Component || !health.Attribute)
            return;

        if (!g_godModeBackup.Valid || g_godModeBackup.Component != health.Component)
        {
            if (g_godModeBackup.Valid && g_godModeBackup.Component)
                Memory::Write(g_godModeBackup.Component + Offsets::HealthComponent_bIsInvulnerable, g_godModeBackup.Invulnerable);
            g_godModeBackup.Component = health.Component;
            g_godModeBackup.Invulnerable = Memory::Read<bool>(health.Component + Offsets::HealthComponent_bIsInvulnerable);
            g_godModeBackup.Valid = true;
        }

        const bool invulnerable = true;
        Memory::Write(health.Component + Offsets::HealthComponent_bIsInvulnerable, invulnerable);
        const float full = health.Maximum;
        Memory::Write(health.Attribute + Offsets::SimpleGameplayAttribute_CurrentData +
                      Offsets::SimpleAttributeData_BaseValue, full);
    }

    void RestoreGodModeFlag()
    {
        if (g_godModeBackup.Valid && g_godModeBackup.Component)
            Memory::Write(g_godModeBackup.Component + Offsets::HealthComponent_bIsInvulnerable, g_godModeBackup.Invulnerable);
        g_godModeBackup = {};
    }

    void RestoreInfiniteStamina()
    {
        // The previous implementation modified RecoverPerSecond and several raw
        // SimpleGameplayAttribute snapshots every frame. That path can trip native
        // stamina delegates and caused a reproducible game crash. The safe version
        // never writes stamina memory directly, so disabling it only resets local
        // bookkeeping and lets the game resume normal drain from its current value.
        g_staminaLastFunctionApply = 0;
        g_staminaFunctionDispatch = false;
        g_staminaCurrent = 0.0f;
        g_staminaMaximum = 0.0f;
        g_staminaValidatedChannels = 0;
        g_staminaFullChannels = 0;
        std::snprintf(g_staminaStatus, sizeof(g_staminaStatus),
            "Safe reflected path idle");
    }

    void ApplyInfiniteStamina()
    {
        const auto& attributes = GameAccess::GetStaminaAttributes();
        if (attributes.empty())
        {
            std::snprintf(g_staminaStatus, sizeof(g_staminaStatus),
                "WAIT: character/arm stamina attributes unavailable");
            return;
        }

        const ULONGLONG now = GetTickCount64();
        const bool canApply = !g_staminaLastFunctionApply ||
            now - g_staminaLastFunctionApply >= 200;
        int validated = 0;
        int full = 0;
        int needed = 0;
        int dispatched = 0;
        float lowestRatio = 2.0f;
        float lowestCurrent = 0.0f;
        float lowestMaximum = 0.0f;

        for (const uintptr_t attribute : attributes)
        {
            if (!attribute)
                continue;

            float current = 0.0f;
            float maximum = 0.0f;
            const bool currentOk = GameAccess::QueryFloatFunction(attribute,
                FunctionIndices::SimpleGameplayAttribute_GetCurrentValue, current);
            const bool maximumOk = GameAccess::QueryFloatFunction(attribute,
                FunctionIndices::SimpleGameplayAttribute_GetCurrentMaxValue, maximum);
            if (!currentOk || !maximumOk || !std::isfinite(current) ||
                !std::isfinite(maximum) || maximum <= 0.0f || maximum > 100000.0f ||
                current < -1000.0f || current > maximum * 4.0f)
                continue;

            ++validated;
            float observed = current;
            if (current < maximum - 0.01f)
            {
                ++needed;
                if (canApply)
                {
                    float params = maximum;
                    const bool applied = GameAccess::InvokeFunctionRaw(attribute,
                        FunctionIndices::SimpleGameplayAttribute_SetBaseValue,
                        &params, sizeof(params));
                    if (applied)
                    {
                        ++dispatched;
                        if (GameAccess::QueryFloatFunction(attribute,
                                FunctionIndices::SimpleGameplayAttribute_GetCurrentValue,
                                observed) && std::isfinite(observed))
                            current = observed;
                    }
                }
            }

            if (current >= maximum - 0.01f)
                ++full;
            const float ratio = current / maximum;
            if (ratio < lowestRatio)
            {
                lowestRatio = ratio;
                lowestCurrent = current;
                lowestMaximum = maximum;
            }
        }

        g_staminaValidatedChannels = validated;
        g_staminaFullChannels = full;
        g_staminaCurrent = lowestCurrent;
        g_staminaMaximum = lowestMaximum;
        if (canApply && needed > 0)
        {
            g_staminaLastFunctionApply = now;
            g_staminaFunctionDispatch = dispatched == needed;
        }

        if (validated == 0)
        {
            std::snprintf(g_staminaStatus, sizeof(g_staminaStatus),
                "BLOCKED: both stamina channels failed validation");
        }
        else if (full == validated)
        {
            std::snprintf(g_staminaStatus, sizeof(g_staminaStatus),
                "SAFE: %d/%d character + arm channels full", full, validated);
        }
        else if (!canApply)
        {
            std::snprintf(g_staminaStatus, sizeof(g_staminaStatus),
                "MONITOR: %d/%d channels full", full, validated);
        }
        else
        {
            std::snprintf(g_staminaStatus, sizeof(g_staminaStatus),
                "%s: %d/%d channels restored",
                g_staminaFunctionDispatch ? "SAFE SetBaseValue" : "PARTIAL DISPATCH",
                dispatched, needed);
        }
    }

    void ApplyNoRecoil()
    {
        const uintptr_t recoil = GameAccess::GetWeaponRecoilObject();
        if (!recoil)
            return;
        const FVector zero{};
        for (const uintptr_t offset : {
            Offsets::FirstPersonWeaponRecoil_TargetCameraRecoil,
            Offsets::FirstPersonWeaponRecoil_TargetCameraRecoilCompensation,
            Offsets::FirstPersonWeaponRecoil_CurrentViewmodelRecoilRotation,
            Offsets::FirstPersonWeaponRecoil_CurrentViewmodelRecoilLocation,
            Offsets::FirstPersonWeaponRecoil_CurrentCameraRecoil,
            Offsets::FirstPersonWeaponRecoil_CurrentCameraRecoilCompensation,
            Offsets::FirstPersonWeaponRecoil_FinalViewmodelRecoilRotation,
            Offsets::FirstPersonWeaponRecoil_FinalViewmodelRecoilLocation })
            Memory::Write(recoil + offset, zero);
    }

    void ApplyNoWeaponSway()
    {
        const uintptr_t staminaArm = GameAccess::GetStaminaArmObject();
        if (!staminaArm)
            return;
        const FVector zero{};
        Memory::Write(staminaArm + Offsets::FirstPersonStaminaArm_ArmSwayLocation, zero);
        Memory::Write(staminaArm + Offsets::FirstPersonStaminaArm_ArmSwayRotation, zero);
    }

    constexpr std::array<uintptr_t, 4> WeaponConditionOffsets = {
        Offsets::WeaponComponent_DegradationRateSemiMultiplier,
        Offsets::WeaponComponent_DegradationRateAutoMultiplier,
        Offsets::WeaponComponent_MisfireChance,
        Offsets::WeaponComponent_BlockChance
    };

    void RestoreWeaponCondition()
    {
        if (g_weaponConditionBackup.Valid && g_weaponConditionBackup.Component)
            for (size_t i = 0; i < WeaponConditionOffsets.size(); ++i)
                Memory::Write(g_weaponConditionBackup.Component + WeaponConditionOffsets[i],
                              g_weaponConditionBackup.Values[i]);
        g_weaponConditionBackup = {};
    }

    void ApplyNoMalfunctions()
    {
        const uintptr_t component = GameAccess::GetWeaponComponent();
        if (!component)
            return;
        if (!g_weaponConditionBackup.Valid || g_weaponConditionBackup.Component != component)
        {
            RestoreWeaponCondition();
            WeaponConditionBackup backup{};
            backup.Component = component;
            for (size_t i = 0; i < WeaponConditionOffsets.size(); ++i)
            {
                const float value = Memory::Read<float>(component + WeaponConditionOffsets[i]);
                if (!std::isfinite(value) || std::abs(value) > 100000.0f)
                    return;
                backup.Values[i] = value;
            }
            backup.Valid = true;
            g_weaponConditionBackup = backup;
        }
        const float zero = 0.0f;
        for (const uintptr_t offset : WeaponConditionOffsets)
            Memory::Write(component + offset, zero);
    }

    void RestoreCarryWeight()
    {
        if (g_weightBackup.Valid && g_weightBackup.Object)
            Memory::Write(g_weightBackup.Object + Offsets::InventoryComponent_MaxWeight,
                          g_weightBackup.Value);
        g_weightBackup = {};
    }

    void ApplyCarryWeight(float value)
    {
        const uintptr_t inventory = GameAccess::GetInventoryComponent();
        if (!inventory)
            return;
        if (!g_weightBackup.Valid || g_weightBackup.Object != inventory)
        {
            RestoreCarryWeight();
            const float original = Memory::Read<float>(inventory +
                Offsets::InventoryComponent_MaxWeight);
            if (!std::isfinite(original) || original < 0.0f || original > 1000000.0f)
                return;
            g_weightBackup = { inventory, original, true };
        }
        Memory::Write(inventory + Offsets::InventoryComponent_MaxWeight,
                      std::clamp(value, 1.0f, 100000.0f));
    }

    void RestoreInvisibleComponent()
    {
        if (g_invisibleBackup.Valid && g_invisibleBackup.Component)
        {
            GameAccess::InvokeBooleanFunction(g_invisibleBackup.Component,
                FunctionIndices::SenseStimulusComponent_SetInvisibleServer,
                g_invisibleBackup.OriginalValue);
            Memory::Write(g_invisibleBackup.Component + Offsets::SenseStimulusBase_bInvisible,
                          g_invisibleBackup.OriginalValue);
        }
        g_invisibleBackup = {};
        g_invisibleAppliedComponent = 0;
    }

    void ApplyInvisible()
    {
        const auto& diagnostics = GameAccess::GetDiagnostics();
        const uintptr_t component = diagnostics.SenseStimulusComponent;
        if (!component || !diagnostics.SenseStimulusComponentTypeValid)
            return;

        if (!g_invisibleBackup.Valid || g_invisibleBackup.Component != component)
        {
            RestoreInvisibleComponent();
            bool original = false;
            if (!Memory::TryRead(component + Offsets::SenseStimulusBase_bInvisible,
                                 original))
                return;
            g_invisibleBackup = { component, original, true };
        }

        const bool invisible = true;
        Memory::Write(component + Offsets::SenseStimulusBase_bInvisible, invisible);
        if (g_invisibleAppliedComponent != component &&
            GameAccess::InvokeBooleanFunction(component,
                FunctionIndices::SenseStimulusComponent_SetInvisibleServer, true))
            g_invisibleAppliedComponent = component;

        ApplyGameInstanceToggle(true,
            FunctionIndices::GeneralGameInstance_ToggleInvisible,
            g_invisibleAppliedInstance);
    }

    void RestoreInvisible()
    {
        ApplyGameInstanceToggle(false,
            FunctionIndices::GeneralGameInstance_ToggleInvisible,
            g_invisibleAppliedInstance);
        RestoreInvisibleComponent();
    }

    void ApplyGameInstanceToggle(bool enabled, int32_t functionIndex,
                                 uintptr_t& appliedInstance)
    {
        const uintptr_t gameInstance = GameAccess::GetGameInstance();
        if (!gameInstance)
            return;
        if (appliedInstance != gameInstance || !enabled)
        {
            if (GameAccess::InvokeBooleanFunction(gameInstance, functionIndex, enabled))
                appliedInstance = enabled ? gameInstance : 0;
        }
    }
}

namespace UECheats
{
    bool bGodMode = false;
    bool bInfiniteAmmo = false;
    bool bNoReload = false;
    bool bSpeedHack = false;
    bool bInfiniteStamina = false;
    bool bNoRecoil = false;
    bool bNoWeaponSway = false;
    bool bNoMalfunctions = false;
    bool bHighCarryWeight = false;
    bool bInvisible = false;
    bool bBulletDebugTraces = false;
    bool bFly = false;
    bool bNoClip = false;
    float speedMultiplier = 2.0f;
    float carryWeight = 1000.0f;
    float flySpeed = 1200.0f;

    void EnableGodMode()
    {
        bGodMode = true;
        ApplyGodMode();
        ApplyGameInstanceToggle(true,
            FunctionIndices::GeneralGameInstance_ToggleInvulnerable,
            g_invulnerableAppliedInstance);
    }

    void DisableGodMode()
    {
        bGodMode = false;
        ApplyGameInstanceToggle(false,
            FunctionIndices::GeneralGameInstance_ToggleInvulnerable,
            g_invulnerableAppliedInstance);
        RestoreGodModeFlag();
    }

    void EnableInfiniteAmmo()
    {
        bInfiniteAmmo = true;
        ApplyInfiniteAmmo();
    }

    void DisableInfiniteAmmo()
    {
        bInfiniteAmmo = false;
        if (!bNoReload)
            RestoreInfiniteAmmo();
    }

    void EnableNoReload() { bNoReload = true; ApplyInfiniteAmmo(); }
    void DisableNoReload()
    {
        bNoReload = false;
        if (!bInfiniteAmmo)
            RestoreInfiniteAmmo();
    }

    void EnableSpeedHack()
    {
        bSpeedHack = true;
        ApplySpeed(speedMultiplier);
    }

    void DisableSpeedHack()
    {
        bSpeedHack = false;
        RestoreSpeed();
    }

    void ProcessTick()
    {
        if (bGodMode)
        {
            ApplyGodMode();
            ApplyGameInstanceToggle(true,
                FunctionIndices::GeneralGameInstance_ToggleInvulnerable,
                g_invulnerableAppliedInstance);
        }
        if (bInfiniteAmmo || bNoReload)
            ApplyInfiniteAmmo();
        if (bSpeedHack)
            ApplySpeed(speedMultiplier);
        if (bNoClip)
            ApplyNoClip();
        else if (bFly)
            ApplyFly(flySpeed);
        if (bInfiniteStamina)
            ApplyInfiniteStamina();
        if (bNoRecoil)
            ApplyNoRecoil();
        if (bNoWeaponSway)
            ApplyNoWeaponSway();
        if (bNoMalfunctions)
            ApplyNoMalfunctions();
        if (bHighCarryWeight)
            ApplyCarryWeight(carryWeight);
        if (bInvisible)
            ApplyInvisible();
        if (bBulletDebugTraces)
            ApplyGameInstanceToggle(true,
                FunctionIndices::GeneralGameInstance_ToggleBulletDebugTraces,
                g_bulletTraceAppliedInstance);
    }

    void Shutdown()
    {
        DisableGodMode();
        bNoReload = false;
        DisableInfiniteAmmo();
        DisableSpeedHack();
        bFly = false;
        bNoClip = false;
        RestoreNoClip();
        RestoreFly();
        bInfiniteStamina = false;
        RestoreInfiniteStamina();
        bNoRecoil = false;
        bNoWeaponSway = false;
        bNoMalfunctions = false;
        RestoreWeaponCondition();
        bHighCarryWeight = false;
        RestoreCarryWeight();
        bInvisible = false;
        RestoreInvisible();
        bBulletDebugTraces = false;
        ApplyGameInstanceToggle(false,
            FunctionIndices::GeneralGameInstance_ToggleBulletDebugTraces,
            g_bulletTraceAppliedInstance);
    }

    void RenderTab()
    {
        if (ImGui::Checkbox("God Mode", &bGodMode))
            bGodMode ? EnableGodMode() : DisableGodMode();

        if (ImGui::Checkbox("Speed Multiplier", &bSpeedHack))
            bSpeedHack ? EnableSpeedHack() : DisableSpeedHack();
        if (bSpeedHack)
            ImGui::SliderFloat("Speed", &speedMultiplier, 0.5f, 4.0f, "%.2fx");

        if (ImGui::Checkbox("Fly", &bFly))
        {
            if (bFly) ApplyFly(flySpeed);
            else if (!bNoClip) RestoreFly();
        }
        if (bFly || bNoClip)
        {
            ImGui::SliderFloat("Fly Speed", &flySpeed, 100.0f, 5000.0f, "%.0f");
            ImGui::TextDisabled("Space: ascend | Left/Right Ctrl: descend | WASD: move");
        }

        if (ImGui::Checkbox("No Clip (includes fly)", &bNoClip))
        {
            if (bNoClip)
                ApplyNoClip();
            else
            {
                RestoreNoClip();
                if (!bFly)
                    RestoreFly();
            }
        }
        ImGui::TextDisabled("No Clip disables actor + capsule collision and restores both original states.");

        if (ImGui::Checkbox("Infinite Stamina (safe reflected path)", &bInfiniteStamina))
        {
            if (bInfiniteStamina) ApplyInfiniteStamina();
            else RestoreInfiniteStamina();
        }
        ImGui::Text("Stamina channels: %d discovered | %d validated | %d full",
            GameAccess::GetDiagnostics().StaminaAttributeCount,
            g_staminaValidatedChannels, g_staminaFullChannels);
        ImGui::Text("Lowest channel current/max: %.1f / %.1f",
            g_staminaCurrent, g_staminaMaximum);
        ImGui::TextDisabled("%s", g_staminaStatus);
        ImGui::TextDisabled("Covers character/sprint and arm/aim stamina; no raw attribute writes are used.");
        if (ImGui::Checkbox("Invisible to AI", &bInvisible))
        {
            if (bInvisible) ApplyInvisible();
            else RestoreInvisible();
        }
        const auto& liveDiagnostics = GameAccess::GetDiagnostics();
        ImGui::Text("AI stimulus: 0x%llX | type: %s | invisible flag: %s",
            static_cast<unsigned long long>(liveDiagnostics.SenseStimulusComponent),
            liveDiagnostics.SenseStimulusComponentTypeValid ? "VALID" : "INVALID",
            liveDiagnostics.SenseStimulusComponent ?
                (Memory::Read<bool>(liveDiagnostics.SenseStimulusComponent +
                    Offsets::SenseStimulusBase_bInvisible) ? "ON" : "OFF") : "WAIT");

        ImGui::Separator();
        ImGui::Text("Inventory:");
        if (ImGui::Checkbox("High Carry Weight (test)", &bHighCarryWeight))
        {
            if (bHighCarryWeight) ApplyCarryWeight(carryWeight);
            else RestoreCarryWeight();
        }
        if (bHighCarryWeight)
            ImGui::SliderFloat("Max Carry Weight", &carryWeight,
                               100.0f, 5000.0f, "%.0f");

        ImGui::Separator();
        ImGui::Text("Weapon:");
        if (ImGui::Checkbox("Infinite Ammo", &bInfiniteAmmo))
            bInfiniteAmmo ? EnableInfiniteAmmo() : DisableInfiniteAmmo();
        if (ImGui::Checkbox("No Reload (game infinite-ammo path)", &bNoReload))
            bNoReload ? EnableNoReload() : DisableNoReload();
        ImGui::Checkbox("No Recoil (runtime recoil state, test)", &bNoRecoil);
        ImGui::Checkbox("No Weapon Sway (runtime arm sway, test)", &bNoWeaponSway);
        if (ImGui::Checkbox("No Malfunctions / Durability Loss (test)",
                            &bNoMalfunctions))
        {
            if (bNoMalfunctions) ApplyNoMalfunctions();
            else RestoreWeaponCondition();
        }

        const uintptr_t gameInstance = GameAccess::GetGameInstance();
        if (gameInstance)
        {
            const bool gameFlag = Memory::Read<bool>(gameInstance + Offsets::GeneralGameInstance_bInfiniteAmmunition);
            ImGui::Text("Game infinite-ammo flag: %s", gameFlag ? "ON" : "OFF");
        }
        else
        {
            ImGui::TextDisabled("Game infinite-ammo flag: no GameInstance");
        }

        const uintptr_t weaponComponent = GetWeaponComponent();
        if (weaponComponent)
        {
            const uintptr_t state = weaponComponent + Offsets::WeaponComponent_AmmunitionState;
            const int capacity = Memory::Read<int>(state + Offsets::AmmunitionState_MagCapacity);
            const bool chamberEmpty = Memory::Read<bool>(state + Offsets::AmmunitionState_bIsChamberEmpty);
            ImGui::Text("Weapon snapshot: %d / chamber %s", capacity, chamberEmpty ? "EMPTY" : "LOADED");
        }
        else
        {
            ImGui::TextDisabled("Weapon snapshot: no equipped BP_MasterWeapon");
        }

        const auto& diagnostics = GameAccess::GetDiagnostics();
        ImGui::Text("Ammo function dispatch: %s | ProcessEvent: %s",
            diagnostics.LastProcessEventCallSucceeded ? "OK" : "WAIT / FAILED",
            diagnostics.ProcessEventValid ? "VALID" : "UNVALIDATED");
        ImGui::Text("Inventory GetInfiniteAmmunition: %s",
            g_ammoFunctionStateValid ? (g_ammoFunctionState ? "ON" : "OFF") : "WAIT");
        ImGui::TextWrapped("Infinite Ammo now calls both the dump-confirmed GeneralGameInstance and live InventoryComponent functions. The reflected game flag remains enabled as reinforcement; AmmunitionState is diagnostic-only.");

        ImGui::Separator();
        ImGui::Text("Diagnostics / visual tests:");
        if (ImGui::Checkbox("Bullet Debug Traces (game function, test)",
                            &bBulletDebugTraces))
            ApplyGameInstanceToggle(bBulletDebugTraces,
                FunctionIndices::GeneralGameInstance_ToggleBulletDebugTraces,
                g_bulletTraceAppliedInstance);
        ImGui::TextWrapped("These toggles use fields/functions from the updated Dumper-7 output and resolve their owning live objects dynamically. Fly uses the validated CharacterMovement component. No Clip uses the reflected actor and primitive collision functions and restores both captured states when disabled.");
    }
}
