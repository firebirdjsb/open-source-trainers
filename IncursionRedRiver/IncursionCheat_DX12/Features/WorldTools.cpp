#include "WorldTools.h"

#include "../Memory/Memory.h"
#include "../sdk/GameAccess.h"
#include "../sdk/Offsets.h"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    char g_status[192] = "No world/debug operation submitted yet.";
    int g_hour = 12;
    int g_weatherType = 0;
    float g_weatherTransition = 1.0f;
    float g_durability = 100.0f;
    float g_successRate = 100.0f;
    int g_aiOptimizationLevel = 0;

    bool g_debugOperations = false;
    bool g_aiOptimization = false;
    bool g_cheatMovement = false;
    bool g_lowLumen = false;
    bool g_extendedParty = false;
    bool g_restock = false;

    struct BoolBackup
    {
        uintptr_t Instance = 0;
        bool Value = false;
        bool Valid = false;
    };

    BoolBackup g_extendedPartyBackup{};
    BoolBackup g_restockBackup{};
    uintptr_t g_appliedGameInstance = 0;
    ULONGLONG g_lastReapply = 0;
    char g_resetConfirmation[16]{};

    uintptr_t GameInstance()
    {
        const auto& d = GameAccess::GetDiagnostics();
        return d.GameInstanceTypeValid ? GameAccess::GetGameInstance() : 0;
    }

    void SetStatus(const char* label, bool ok)
    {
        std::snprintf(g_status, sizeof(g_status), "%s: %s", label,
            ok ? "ProcessEvent dispatched" : "blocked / validation failed");
    }

    bool Call0(int32_t index)
    {
        const uintptr_t gi = GameInstance();
        return gi && GameAccess::InvokeFunctionRaw(gi, index, nullptr, 0);
    }

    bool CallBool(int32_t index, bool value)
    {
        const uintptr_t gi = GameInstance();
        return gi && GameAccess::InvokeBooleanFunction(gi, index, value);
    }

    template <typename T>
    bool CallValue(int32_t index, const T& value)
    {
        const uintptr_t gi = GameInstance();
        if (!gi)
            return false;
        T params = value;
        return GameAccess::InvokeFunctionRaw(gi, index, &params, sizeof(params));
    }

    void CaptureBool(BoolBackup& backup, uintptr_t gi, uintptr_t offset)
    {
        if (backup.Valid && backup.Instance == gi)
            return;
        backup = {};
        if (!gi || !Memory::IsReadable(gi + offset, sizeof(bool)))
            return;
        backup.Instance = gi;
        backup.Value = Memory::Read<bool>(gi + offset);
        backup.Valid = true;
    }

    void RestoreBool(BoolBackup& backup, int32_t functionIndex)
    {
        if (backup.Valid && backup.Instance)
            GameAccess::InvokeBooleanFunction(backup.Instance, functionIndex, backup.Value);
        backup = {};
    }

    void ReapplyStatefulToggles(bool force)
    {
        const uintptr_t gi = GameInstance();
        if (!gi)
            return;
        const ULONGLONG now = GetTickCount64();
        if (!force && g_appliedGameInstance == gi && g_lastReapply && now - g_lastReapply < 1000)
            return;

        CaptureBool(g_extendedPartyBackup, gi, Offsets::GeneralGameInstance_bExtendedPartyEnabled);
        CaptureBool(g_restockBackup, gi, Offsets::GeneralGameInstance_bRestock);

        if (g_debugOperations)
            GameAccess::InvokeBooleanFunction(gi,
                FunctionIndices::GeneralGameInstance_ToggleDebugOperations, true);
        if (g_aiOptimization)
            GameAccess::InvokeBooleanFunction(gi,
                FunctionIndices::GeneralGameInstance_ToggleAIOptimization, true);
        if (g_cheatMovement)
            GameAccess::InvokeBooleanFunction(gi,
                FunctionIndices::GeneralGameInstance_ToggleCheatMovementSpeed, true);
        if (g_lowLumen)
            GameAccess::InvokeBooleanFunction(gi,
                FunctionIndices::GeneralGameInstance_SetLumenScalability, true);
        if (g_extendedParty)
            GameAccess::InvokeBooleanFunction(gi,
                FunctionIndices::GeneralGameInstance_ToggleExtendedParty, true);
        if (g_restock)
            GameAccess::InvokeBooleanFunction(gi,
                FunctionIndices::GeneralGameInstance_SetRestock, true);
        g_appliedGameInstance = gi;
        g_lastReapply = now;
    }

    bool ResetUnlocked()
    {
        return std::strcmp(g_resetConfirmation, "RESET") == 0;
    }
}

namespace WorldTools
{
    void ProcessTick()
    {
        ReapplyStatefulToggles(false);
    }

    void Shutdown()
    {
        const uintptr_t gi = GameInstance();
        if (gi)
        {
            if (g_debugOperations)
                GameAccess::InvokeBooleanFunction(gi,
                    FunctionIndices::GeneralGameInstance_ToggleDebugOperations, false);
            if (g_aiOptimization)
                GameAccess::InvokeBooleanFunction(gi,
                    FunctionIndices::GeneralGameInstance_ToggleAIOptimization, false);
            if (g_cheatMovement)
                GameAccess::InvokeBooleanFunction(gi,
                    FunctionIndices::GeneralGameInstance_ToggleCheatMovementSpeed, false);
            if (g_lowLumen)
                GameAccess::InvokeBooleanFunction(gi,
                    FunctionIndices::GeneralGameInstance_SetLumenScalability, false);
        }
        RestoreBool(g_extendedPartyBackup,
            FunctionIndices::GeneralGameInstance_ToggleExtendedParty);
        RestoreBool(g_restockBackup,
            FunctionIndices::GeneralGameInstance_SetRestock);
        g_debugOperations = false;
        g_aiOptimization = false;
        g_cheatMovement = false;
        g_lowLumen = false;
        g_extendedParty = false;
        g_restock = false;
        g_appliedGameInstance = 0;
        g_lastReapply = 0;
    }

    void RenderTab()
    {
        const uintptr_t gi = GameInstance();
        ImGui::Text("Game world controls | GeneralGameInstance: %s", gi ? "VALID" : "WAIT");
        ImGui::TextWrapped("These controls call named game-owned GeneralGameInstance UFunctions from the current Dumper-7 layout. No guessed manager offsets are used.");

        ImGui::Separator();
        ImGui::Text("Raid / mission quick actions:");
        if (ImGui::Button("Force extraction"))
            SetStatus("ForceExtraction", Call0(FunctionIndices::GeneralGameInstance_ForceExtraction));
        ImGui::SameLine();
        if (ImGui::Button("Kill all AI"))
            SetStatus("KillAllAI", Call0(FunctionIndices::GeneralGameInstance_KillAllAI));
        ImGui::SameLine();
        if (ImGui::Button("Show all AI"))
            SetStatus("ShowAllAI", Call0(FunctionIndices::GeneralGameInstance_ShowAllAI));

        if (ImGui::Button("Complete tracked missions"))
            SetStatus("CompleteTrackedMissions", Call0(FunctionIndices::GeneralGameInstance_CompleteTrackedMissions));
        ImGui::SameLine();
        if (ImGui::Button("Fail tracked missions"))
            SetStatus("FailTrackedMissions", Call0(FunctionIndices::GeneralGameInstance_FailTrackedMissions));
        ImGui::SameLine();
        if (ImGui::Button("Regenerate missions"))
            SetStatus("RegenerateMissions", Call0(FunctionIndices::GeneralGameInstance_RegenerateMissions));

        ImGui::Separator();
        ImGui::Text("World state:");
        ImGui::SliderInt("Hour (24h)", &g_hour, 0, 23);
        if (ImGui::Button("Set time of day"))
            SetStatus("SetTimeOfDay", CallValue(FunctionIndices::GeneralGameInstance_SetTimeOfDay, g_hour));

        ImGui::InputInt("Weather type index", &g_weatherType, 1, 1);
        g_weatherType = std::clamp(g_weatherType, 0, 32);
        ImGui::SliderFloat("Weather transition seconds", &g_weatherTransition, 0.0f, 60.0f, "%.1f");
        if (ImGui::Button("Set weather"))
        {
            struct Params { int32_t WeatherType; float TransitionTime; } params{
                g_weatherType, std::max(0.0f, g_weatherTransition) };
            const bool ok = gi && GameAccess::InvokeFunctionRaw(gi,
                FunctionIndices::GeneralGameInstance_SetWeatherType,
                &params, sizeof(params));
            SetStatus("SetWeatherType", ok);
        }
        ImGui::TextDisabled("Weather is an integer in the dump; use small indexes first because the enum display names are not emitted by Dumper-7.");

        ImGui::Separator();
        ImGui::Text("Game debug / tuning:");
        if (ImGui::Checkbox("Enable debug operations", &g_debugOperations))
        {
            SetStatus("ToggleDebugOperations", CallBool(
                FunctionIndices::GeneralGameInstance_ToggleDebugOperations,
                g_debugOperations));
            ReapplyStatefulToggles(true);
        }

        if (ImGui::Checkbox("AI optimization", &g_aiOptimization))
        {
            SetStatus("ToggleAIOptimization", CallBool(
                FunctionIndices::GeneralGameInstance_ToggleAIOptimization,
                g_aiOptimization));
            ReapplyStatefulToggles(true);
        }
        ImGui::InputInt("AI optimization level", &g_aiOptimizationLevel, 1, 1);
        g_aiOptimizationLevel = std::clamp(g_aiOptimizationLevel, 0, 8);
        ImGui::SameLine();
        if (ImGui::Button("Apply AI level"))
            SetStatus("SetAIOptimizationLevel", CallValue(
                FunctionIndices::GeneralGameInstance_SetAIOptimizationLevel,
                g_aiOptimizationLevel));

        if (ImGui::Checkbox("Extended party", &g_extendedParty))
        {
            SetStatus("ToggleExtendedParty", CallBool(
                FunctionIndices::GeneralGameInstance_ToggleExtendedParty,
                g_extendedParty));
            ReapplyStatefulToggles(true);
        }
        if (ImGui::Checkbox("Force vendor restock state", &g_restock))
        {
            SetStatus("SetRestock", CallBool(
                FunctionIndices::GeneralGameInstance_SetRestock, g_restock));
            ReapplyStatefulToggles(true);
        }

        ImGui::SliderFloat("Set weapon durability %", &g_durability, 0.0f, 100.0f, "%.0f%%");
        if (ImGui::Button("Apply durability"))
            SetStatus("SetDurability", CallValue(
                FunctionIndices::GeneralGameInstance_SetDurability, g_durability));
        ImGui::SameLine();
        ImGui::SliderFloat("Mission success rate", &g_successRate, 0.0f, 100.0f, "%.0f%%");
        if (ImGui::Button("Apply success rate"))
            SetStatus("SetSuccessRate", CallValue(
                FunctionIndices::GeneralGameInstance_SetSuccessRate, g_successRate));

        if (ImGui::Button("Toggle cinematic camera"))
            SetStatus("ToggleCinematicCamera", Call0(
                FunctionIndices::GeneralGameInstance_ToggleCinematicCamera));
        if (ImGui::Checkbox("Built-in cheat movement speed", &g_cheatMovement))
        {
            SetStatus("ToggleCheatMovementSpeed", CallBool(
                FunctionIndices::GeneralGameInstance_ToggleCheatMovementSpeed,
                g_cheatMovement));
            ReapplyStatefulToggles(true);
        }

        if (ImGui::Checkbox("Low Lumen scalability (performance)", &g_lowLumen))
        {
            SetStatus("SetLumenScalability", CallBool(
                FunctionIndices::GeneralGameInstance_SetLumenScalability,
                g_lowLumen));
            ReapplyStatefulToggles(true);
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Danger zone - destructive debug resets"))
        {
            ImGui::TextWrapped("These are real game reset functions and can change saved/profile state. Type RESET to unlock them for this menu session.");
            ImGui::InputText("Confirmation", g_resetConfirmation, sizeof(g_resetConfirmation));
            const bool unlocked = ResetUnlocked();
            ImGui::BeginDisabled(!unlocked);
            if (ImGui::Button("Reset inventory"))
                SetStatus("ResetInventory", Call0(FunctionIndices::GeneralGameInstance_ResetInventory));
            ImGui::SameLine();
            if (ImGui::Button("Reset resources"))
                SetStatus("ResetResources", Call0(FunctionIndices::GeneralGameInstance_ResetResources));
            if (ImGui::Button("Reset mission system"))
                SetStatus("ResetMissionSystem", Call0(FunctionIndices::GeneralGameInstance_ResetMissionSystem));
            ImGui::SameLine();
            if (ImGui::Button("Reset tracked missions"))
                SetStatus("ResetTrackedMissions", Call0(FunctionIndices::GeneralGameInstance_ResetTrackedMissions));
            if (ImGui::Button("Reset raid data"))
                SetStatus("ResetRaidData", Call0(FunctionIndices::GeneralGameInstance_ResetRaidData));
            ImGui::SameLine();
            if (ImGui::Button("Reset vendor"))
                SetStatus("ResetVendor", Call0(FunctionIndices::GeneralGameInstance_ResetVendor));
            if (ImGui::Button("Reset achievements"))
                SetStatus("ResetAchievements", Call0(FunctionIndices::GeneralGameInstance_ResetAchievements));
            ImGui::SameLine();
            if (ImGui::Button("Reset world"))
                SetStatus("ResetWorld", Call0(FunctionIndices::GeneralGameInstance_ResetWorld));
            ImGui::EndDisabled();
        }

        ImGui::Separator();
        ImGui::TextWrapped("Last operation: %s", g_status);
        ImGui::TextDisabled("Stateful toggles are only reasserted once per second and only while enabled; one-shot actions do no per-frame work.");
    }
}
