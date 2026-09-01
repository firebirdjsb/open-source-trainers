#include "MovementTools.h"

#include "../Memory/Memory.h"
#include "../sdk/GameAccess.h"
#include "../sdk/Offsets.h"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    struct MovementBackup
    {
        uintptr_t Component = 0;
        float MaxStepHeight = 0.0f;
        float JumpZVelocity = 0.0f;
        float WalkableFloorAngle = 0.0f;
        float GroundFriction = 0.0f;
        float MaxSwimSpeed = 0.0f;
        float MaxCustomMovementSpeed = 0.0f;
        float MaxAcceleration = 0.0f;
        float AirControl = 0.0f;
        bool Valid = false;
    };

    MovementBackup g_backup{};
    uintptr_t g_timePawn = 0;
    float g_originalTimeDilation = 1.0f;
    bool g_timeBackupValid = false;
    uintptr_t g_fallHealth = 0;
    float g_originalFallHeight = 0.0f;
    bool g_fallBackupValid = false;

    bool g_timeDilation = false;
    float g_timeDilationValue = 1.5f;
    bool g_noFallDamage = false;
    bool g_jump = false;
    float g_jumpMultiplier = 2.0f;
    bool g_gravity = false;
    float g_gravityMultiplier = 0.5f;
    bool g_airControl = false;
    float g_airControlValue = 1.0f;
    bool g_acceleration = false;
    float g_accelerationMultiplier = 2.0f;
    bool g_groundFriction = false;
    float g_groundFrictionMultiplier = 0.5f;
    bool g_stepHeight = false;
    float g_stepHeightMultiplier = 2.0f;
    bool g_walkableAngle = false;
    float g_walkableAngleValue = 70.0f;
    bool g_swimSpeed = false;
    float g_swimMultiplier = 2.0f;
    bool g_customSpeed = false;
    float g_customSpeedMultiplier = 2.0f;

    bool g_prevJump = false;
    bool g_prevGravity = false;
    bool g_prevAirControl = false;
    bool g_prevAcceleration = false;
    bool g_prevGroundFriction = false;
    bool g_prevStepHeight = false;
    bool g_prevWalkableAngle = false;
    bool g_prevSwimSpeed = false;
    bool g_prevCustomSpeed = false;
    uintptr_t g_gravityComponent = 0;
    float g_gravityOriginal = 1.0f;
    bool g_gravityBackupValid = false;

    float g_teleportMeters = 10.0f;
    FVector g_bookmark{};
    uintptr_t g_bookmarkWorld = 0;
    bool g_bookmarkValid = false;
    char g_status[192] = "Movement+ ready.";
    ULONGLONG g_lastApply = 0;

    uintptr_t Movement()
    {
        const uintptr_t pawn = GameAccess::GetLocalPawn();
        return pawn ? Memory::Read<uintptr_t>(pawn + Offsets::ACharacter_CharacterMovement) : 0;
    }

    bool Plausible(float value, float absoluteLimit = 100000.0f)
    {
        return std::isfinite(value) && std::abs(value) <= absoluteLimit;
    }

    bool CaptureMovement(uintptr_t movement)
    {
        if (!movement)
            return false;
        MovementBackup b{};
        b.Component = movement;
        if (!Memory::TryRead(movement + Offsets::CharacterMovement_MaxStepHeight, b.MaxStepHeight) ||
            !Memory::TryRead(movement + Offsets::CharacterMovement_JumpZVelocity, b.JumpZVelocity) ||
            !Memory::TryRead(movement + Offsets::CharacterMovement_WalkableFloorAngle, b.WalkableFloorAngle) ||
            !Memory::TryRead(movement + Offsets::CharacterMovement_GroundFriction, b.GroundFriction) ||
            !Memory::TryRead(movement + Offsets::CharacterMovement_MaxSwimSpeed, b.MaxSwimSpeed) ||
            !Memory::TryRead(movement + Offsets::CharacterMovement_MaxCustomMovementSpeed, b.MaxCustomMovementSpeed) ||
            !Memory::TryRead(movement + Offsets::CharacterMovement_MaxAcceleration, b.MaxAcceleration) ||
            !Memory::TryRead(movement + Offsets::CharacterMovement_AirControl, b.AirControl))
            return false;
        if (!Plausible(b.MaxStepHeight) || !Plausible(b.JumpZVelocity) ||
            !Plausible(b.WalkableFloorAngle, 360.0f) || !Plausible(b.GroundFriction) ||
            !Plausible(b.MaxSwimSpeed) || !Plausible(b.MaxCustomMovementSpeed) ||
            !Plausible(b.MaxAcceleration) || !Plausible(b.AirControl, 100.0f))
            return false;
        b.Valid = true;
        g_backup = b;
        g_prevJump = g_prevGravity = g_prevAirControl = g_prevAcceleration = false;
        g_prevGroundFriction = g_prevStepHeight = g_prevWalkableAngle = false;
        g_prevSwimSpeed = g_prevCustomSpeed = false;
        return true;
    }

    void RestoreMovementAll()
    {
        if (g_backup.Valid && g_backup.Component)
        {
            Memory::Write(g_backup.Component + Offsets::CharacterMovement_MaxStepHeight, g_backup.MaxStepHeight);
            Memory::Write(g_backup.Component + Offsets::CharacterMovement_JumpZVelocity, g_backup.JumpZVelocity);
            Memory::Write(g_backup.Component + Offsets::CharacterMovement_WalkableFloorAngle, g_backup.WalkableFloorAngle);
            Memory::Write(g_backup.Component + Offsets::CharacterMovement_GroundFriction, g_backup.GroundFriction);
            Memory::Write(g_backup.Component + Offsets::CharacterMovement_MaxSwimSpeed, g_backup.MaxSwimSpeed);
            Memory::Write(g_backup.Component + Offsets::CharacterMovement_MaxCustomMovementSpeed, g_backup.MaxCustomMovementSpeed);
            Memory::Write(g_backup.Component + Offsets::CharacterMovement_MaxAcceleration, g_backup.MaxAcceleration);
            Memory::Write(g_backup.Component + Offsets::CharacterMovement_AirControl, g_backup.AirControl);
        }
        g_backup = {};
        g_prevJump = g_prevGravity = g_prevAirControl = g_prevAcceleration = false;
        g_prevGroundFriction = g_prevStepHeight = g_prevWalkableAngle = false;
        g_prevSwimSpeed = g_prevCustomSpeed = false;
        if (g_gravityBackupValid && g_gravityComponent)
            Memory::Write(g_gravityComponent + Offsets::CharacterMovement_GravityScale, g_gravityOriginal);
        g_gravityComponent = 0;
        g_gravityOriginal = 1.0f;
        g_gravityBackupValid = false;
    }

    void RestoreTimeDilation()
    {
        if (g_timeBackupValid && g_timePawn)
            Memory::Write(g_timePawn + Offsets::AActor_CustomTimeDilation, g_originalTimeDilation);
        g_timePawn = 0;
        g_originalTimeDilation = 1.0f;
        g_timeBackupValid = false;
    }

    void ApplyTimeDilation()
    {
        const uintptr_t pawn = GameAccess::GetLocalPawn();
        if (!pawn)
            return;
        if (!g_timeBackupValid || g_timePawn != pawn)
        {
            RestoreTimeDilation();
            float original = 1.0f;
            if (!Memory::TryRead(pawn + Offsets::AActor_CustomTimeDilation, original) ||
                !Plausible(original, 100.0f))
                return;
            g_timePawn = pawn;
            g_originalTimeDilation = original;
            g_timeBackupValid = true;
        }
        const float value = std::clamp(g_timeDilationValue, 0.05f, 10.0f);
        Memory::Write(pawn + Offsets::AActor_CustomTimeDilation, value);
    }

    void RestoreNoFall()
    {
        if (g_fallBackupValid && g_fallHealth)
            Memory::Write(g_fallHealth + Offsets::HealthComponent_FallHeightStart, g_originalFallHeight);
        g_fallHealth = 0;
        g_originalFallHeight = 0.0f;
        g_fallBackupValid = false;
    }

    void ApplyNoFall()
    {
        const auto health = GameAccess::GetHealth(GameAccess::GetLocalPawn());
        if (!health.Component)
            return;
        if (!g_fallBackupValid || g_fallHealth != health.Component)
        {
            RestoreNoFall();
            float original = 0.0f;
            if (!Memory::TryRead(health.Component + Offsets::HealthComponent_FallHeightStart, original) ||
                !Plausible(original))
                return;
            g_fallHealth = health.Component;
            g_originalFallHeight = original;
            g_fallBackupValid = true;
        }
        constexpr float effectivelyDisabled = 1000000000.0f;
        Memory::Write(health.Component + Offsets::HealthComponent_FallHeightStart,
                      effectivelyDisabled);
    }

    void ApplyMovementTuning()
    {
        const uintptr_t movement = Movement();
        if (!movement)
            return;
        if (!g_backup.Valid || g_backup.Component != movement)
        {
            RestoreMovementAll();
            if (!CaptureMovement(movement))
                return;
        }

        auto applyMul = [&](bool enabled, bool& previous, uintptr_t offset,
                            float original, float multiplier, float minValue, float maxValue)
        {
            if (enabled)
            {
                const float value = std::clamp(original * multiplier, minValue, maxValue);
                Memory::Write(movement + offset, value);
            }
            else if (previous)
                Memory::Write(movement + offset, original);
            previous = enabled;
        };

        applyMul(g_jump, g_prevJump, Offsets::CharacterMovement_JumpZVelocity,
                 g_backup.JumpZVelocity, std::clamp(g_jumpMultiplier, 0.1f, 10.0f), 0.0f, 100000.0f);
        applyMul(g_acceleration, g_prevAcceleration, Offsets::CharacterMovement_MaxAcceleration,
                 g_backup.MaxAcceleration, std::clamp(g_accelerationMultiplier, 0.1f, 10.0f), 0.0f, 100000.0f);
        applyMul(g_groundFriction, g_prevGroundFriction, Offsets::CharacterMovement_GroundFriction,
                 g_backup.GroundFriction, std::clamp(g_groundFrictionMultiplier, 0.0f, 10.0f), 0.0f, 100000.0f);
        applyMul(g_stepHeight, g_prevStepHeight, Offsets::CharacterMovement_MaxStepHeight,
                 g_backup.MaxStepHeight, std::clamp(g_stepHeightMultiplier, 0.1f, 10.0f), 0.0f, 100000.0f);
        applyMul(g_swimSpeed, g_prevSwimSpeed, Offsets::CharacterMovement_MaxSwimSpeed,
                 g_backup.MaxSwimSpeed, std::clamp(g_swimMultiplier, 0.1f, 10.0f), 0.0f, 100000.0f);
        applyMul(g_customSpeed, g_prevCustomSpeed, Offsets::CharacterMovement_MaxCustomMovementSpeed,
                 g_backup.MaxCustomMovementSpeed, std::clamp(g_customSpeedMultiplier, 0.1f, 10.0f), 0.0f, 100000.0f);

        if (g_airControl)
            Memory::Write(movement + Offsets::CharacterMovement_AirControl,
                          std::clamp(g_airControlValue, 0.0f, 10.0f));
        else if (g_prevAirControl)
            Memory::Write(movement + Offsets::CharacterMovement_AirControl, g_backup.AirControl);
        g_prevAirControl = g_airControl;

        if (g_walkableAngle)
            Memory::Write(movement + Offsets::CharacterMovement_WalkableFloorAngle,
                          std::clamp(g_walkableAngleValue, 0.0f, 89.9f));
        else if (g_prevWalkableAngle)
            Memory::Write(movement + Offsets::CharacterMovement_WalkableFloorAngle,
                          g_backup.WalkableFloorAngle);
        g_prevWalkableAngle = g_walkableAngle;

        // GravityScale is already a validated field used by the working Fly feature.
        // It is kept separate from the capture block because Fly may intentionally
        // own it. Do not fight Fly/NoClip when those systems are active.
        if (g_gravity)
        {
            float liveGravity = 1.0f;
            if (Memory::TryRead(movement + Offsets::CharacterMovement_GravityScale, liveGravity) &&
                Plausible(liveGravity, 100.0f))
            {
                // Fly/NoClip intentionally owns GravityScale=0. Do not capture or
                // overwrite that state. Once normal movement resumes, capture it.
                if ((!g_gravityBackupValid || g_gravityComponent != movement) &&
                    std::abs(liveGravity) > 0.0001f)
                {
                    if (g_gravityBackupValid && g_gravityComponent &&
                        g_gravityComponent != movement)
                        Memory::Write(g_gravityComponent + Offsets::CharacterMovement_GravityScale,
                                      g_gravityOriginal);
                    g_gravityComponent = movement;
                    g_gravityOriginal = liveGravity;
                    g_gravityBackupValid = true;
                }
                if (g_gravityBackupValid && g_gravityComponent == movement &&
                    std::abs(liveGravity) > 0.0001f)
                    Memory::Write(movement + Offsets::CharacterMovement_GravityScale,
                                  std::clamp(g_gravityOriginal * g_gravityMultiplier, 0.0f, 20.0f));
            }
        }
        else if (g_prevGravity && g_gravityBackupValid && g_gravityComponent)
        {
            Memory::Write(g_gravityComponent + Offsets::CharacterMovement_GravityScale,
                          g_gravityOriginal);
            g_gravityComponent = 0;
            g_gravityOriginal = 1.0f;
            g_gravityBackupValid = false;
        }
        g_prevGravity = g_gravity;
    }

    bool SetHealthPercent(float percent)
    {
        const auto health = GameAccess::GetHealth(GameAccess::GetLocalPawn());
        if (!health.Valid || !health.Attribute || health.Maximum <= health.Minimum)
            return false;
        const float clamped = std::clamp(percent, 0.0f, 100.0f);
        const float desired = health.Minimum +
            (health.Maximum - health.Minimum) * (clamped / 100.0f);
        float params = desired;
        const bool dispatched = GameAccess::InvokeFunctionRaw(health.Attribute,
            FunctionIndices::SimpleGameplayAttribute_SetBaseValue,
            &params, sizeof(params));
        if (dispatched)
        {
            Memory::Write(health.Attribute + Offsets::SimpleGameplayAttribute_BaseData +
                          Offsets::SimpleAttributeData_BaseValue, desired);
            Memory::Write(health.Attribute + Offsets::SimpleGameplayAttribute_CurrentData +
                          Offsets::SimpleAttributeData_BaseValue, desired);
            Memory::Write(health.Attribute + Offsets::SimpleGameplayAttribute_OldData +
                          Offsets::SimpleAttributeData_BaseValue, desired);
        }
        return dispatched;
    }

    bool SetActorLocation(const FVector& location)
    {
        const uintptr_t pawn = GameAccess::GetLocalPawn();
        if (!pawn || !location.IsFinite())
            return false;
        alignas(16) std::array<uint8_t, 0x128> params{};
        std::memcpy(params.data(), &location, sizeof(location));
        params[0x18] = 0;   // bSweep
        params[0x120] = 1;  // bTeleport
        const bool dispatched = GameAccess::InvokeFunctionRaw(pawn,
            FunctionIndices::Actor_K2_SetActorLocation,
            params.data(), params.size());
        return dispatched && params[0x121] != 0;
    }

    bool TeleportForward(float meters)
    {
        const auto camera = GameAccess::GetCamera();
        FVector current{};
        if (!camera.Valid || !GameAccess::GetActorLocation(GameAccess::GetLocalPawn(), current))
            return false;
        constexpr double DegToRad = 3.14159265358979323846 / 180.0;
        const double yaw = camera.Rotation.Yaw * DegToRad;
        const double distanceCm = static_cast<double>(meters) * 100.0;
        const FVector destination = current + FVector(std::cos(yaw) * distanceCm,
                                                       std::sin(yaw) * distanceCm,
                                                       0.0);
        return SetActorLocation(destination);
    }

    void SetStatus(const char* action, bool ok)
    {
        std::snprintf(g_status, sizeof(g_status), "%s: %s", action,
            ok ? "OK" : "blocked / validation failed");
    }
}

namespace MovementTools
{
    void ProcessTick()
    {
        const ULONGLONG now = GetTickCount64();
        if (g_lastApply && now - g_lastApply < 100)
            return;
        g_lastApply = now;

        if (g_timeDilation)
            ApplyTimeDilation();
        else if (g_timeBackupValid)
            RestoreTimeDilation();

        if (g_noFallDamage)
            ApplyNoFall();
        else if (g_fallBackupValid)
            RestoreNoFall();

        const bool anyMovement = g_jump || g_gravity || g_airControl || g_acceleration ||
            g_groundFriction || g_stepHeight || g_walkableAngle || g_swimSpeed || g_customSpeed ||
            g_prevJump || g_prevGravity || g_prevAirControl || g_prevAcceleration ||
            g_prevGroundFriction || g_prevStepHeight || g_prevWalkableAngle ||
            g_prevSwimSpeed || g_prevCustomSpeed;
        if (anyMovement)
            ApplyMovementTuning();
    }

    void Shutdown()
    {
        g_timeDilation = false;
        g_noFallDamage = false;
        g_jump = g_gravity = g_airControl = g_acceleration = false;
        g_groundFriction = g_stepHeight = g_walkableAngle = false;
        g_swimSpeed = g_customSpeed = false;
        RestoreTimeDilation();
        RestoreNoFall();
        RestoreMovementAll();
        g_bookmarkValid = false;
        g_bookmarkWorld = 0;
        g_lastApply = 0;
    }

    void RenderTab()
    {
        const uintptr_t pawn = GameAccess::GetLocalPawn();
        const uintptr_t movement = Movement();
        const auto health = GameAccess::GetHealth(pawn);
        ImGui::Text("Movement+ | pawn: %s | movement: %s | health: %s",
            pawn ? "OK" : "WAIT", movement ? "OK" : "WAIT", health.Valid ? "OK" : "WAIT");
        ImGui::TextWrapped("All persistent field edits capture their original value and restore it when disabled or when the DLL shuts down. Writes are throttled to 10 Hz.");

        ImGui::Separator();
        ImGui::Text("Health / survival:");
        if (ImGui::Button("Heal to full"))
            SetStatus("Heal to full", SetHealthPercent(100.0f));
        ImGui::SameLine();
        static float healthPercent = 50.0f;
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat("##healthpct", &healthPercent, 1.0f, 100.0f, "%.0f%%");
        ImGui::SameLine();
        if (ImGui::Button("Set health"))
            SetStatus("Set health", SetHealthPercent(healthPercent));
        if (health.Valid)
            ImGui::Text("Health: %.1f / %.1f", health.Current, health.Maximum);
        ImGui::Checkbox("No fall damage", &g_noFallDamage);

        ImGui::Separator();
        ImGui::Text("Personal time / movement tuning:");
        ImGui::Checkbox("Personal time dilation", &g_timeDilation);
        if (g_timeDilation)
            ImGui::SliderFloat("Time scale", &g_timeDilationValue, 0.05f, 5.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);

        ImGui::Checkbox("Jump multiplier", &g_jump);
        if (g_jump)
            ImGui::SliderFloat("Jump power", &g_jumpMultiplier, 0.25f, 6.0f, "%.2fx");

        ImGui::Checkbox("Gravity multiplier", &g_gravity);
        if (g_gravity)
            ImGui::SliderFloat("Gravity", &g_gravityMultiplier, 0.0f, 3.0f, "%.2fx");
        ImGui::TextDisabled("Gravity tuning yields to the existing Fly/NoClip system when Fly owns GravityScale=0.");

        ImGui::Checkbox("Air control override", &g_airControl);
        if (g_airControl)
            ImGui::SliderFloat("Air control", &g_airControlValue, 0.0f, 5.0f, "%.2f");

        ImGui::Checkbox("Acceleration multiplier", &g_acceleration);
        if (g_acceleration)
            ImGui::SliderFloat("Acceleration", &g_accelerationMultiplier, 0.25f, 6.0f, "%.2fx");

        ImGui::Checkbox("Ground friction multiplier", &g_groundFriction);
        if (g_groundFriction)
            ImGui::SliderFloat("Ground friction", &g_groundFrictionMultiplier, 0.0f, 4.0f, "%.2fx");

        ImGui::Checkbox("Step height multiplier", &g_stepHeight);
        if (g_stepHeight)
            ImGui::SliderFloat("Step height", &g_stepHeightMultiplier, 0.25f, 6.0f, "%.2fx");

        ImGui::Checkbox("Walkable slope override", &g_walkableAngle);
        if (g_walkableAngle)
            ImGui::SliderFloat("Max walkable slope", &g_walkableAngleValue, 0.0f, 89.0f, "%.0f deg");

        ImGui::Checkbox("Swim speed multiplier", &g_swimSpeed);
        if (g_swimSpeed)
            ImGui::SliderFloat("Swim speed", &g_swimMultiplier, 0.25f, 6.0f, "%.2fx");

        ImGui::Checkbox("Custom movement speed multiplier", &g_customSpeed);
        if (g_customSpeed)
            ImGui::SliderFloat("Custom move speed", &g_customSpeedMultiplier, 0.25f, 6.0f, "%.2fx");

        if (ImGui::Button("Reset Movement+ tuning"))
        {
            g_timeDilation = false;
            g_noFallDamage = false;
            g_jump = g_gravity = g_airControl = g_acceleration = false;
            g_groundFriction = g_stepHeight = g_walkableAngle = false;
            g_swimSpeed = g_customSpeed = false;
            RestoreTimeDilation();
            RestoreNoFall();
            RestoreMovementAll();
            SetStatus("Reset Movement+", true);
        }

        ImGui::Separator();
        ImGui::Text("Teleport / bookmark:");
        ImGui::SliderFloat("Forward distance", &g_teleportMeters, 1.0f, 100.0f, "%.1f m");
        if (ImGui::Button("Teleport forward"))
            SetStatus("Teleport forward", TeleportForward(g_teleportMeters));
        ImGui::SameLine();
        if (ImGui::Button("Up 5m"))
        {
            FVector p{};
            SetStatus("Teleport up", GameAccess::GetActorLocation(pawn, p) &&
                SetActorLocation(p + FVector(0.0, 0.0, 500.0)));
        }
        ImGui::SameLine();
        if (ImGui::Button("Down 5m"))
        {
            FVector p{};
            SetStatus("Teleport down", GameAccess::GetActorLocation(pawn, p) &&
                SetActorLocation(p + FVector(0.0, 0.0, -500.0)));
        }

        if (ImGui::Button("Save position"))
        {
            g_bookmarkValid = GameAccess::GetActorLocation(pawn, g_bookmark);
            g_bookmarkWorld = g_bookmarkValid ? GameAccess::GetWorld() : 0;
            SetStatus("Save position", g_bookmarkValid);
        }
        ImGui::SameLine();
        const bool bookmarkUsable = g_bookmarkValid && g_bookmarkWorld == GameAccess::GetWorld();
        ImGui::BeginDisabled(!bookmarkUsable);
        if (ImGui::Button("Return to saved position"))
            SetStatus("Return to saved position", SetActorLocation(g_bookmark));
        ImGui::EndDisabled();
        if (bookmarkUsable)
            ImGui::Text("Saved: %.1f, %.1f, %.1f", g_bookmark.X, g_bookmark.Y, g_bookmark.Z);
        else
            ImGui::TextDisabled("No valid position saved for the current world.");

        ImGui::Separator();
        ImGui::TextWrapped("Last operation: %s", g_status);
    }
}
