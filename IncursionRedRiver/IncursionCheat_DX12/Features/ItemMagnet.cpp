#include "ItemMagnet.h"

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
    float g_rangeMeters = 35.0f;
    float g_dropDistanceMeters = 1.8f;
    float g_dropHeightMeters = 0.35f;
    int g_maxPerPass = 24;
    int g_intervalMs = 250;
    int g_lastMatched = 0;
    int g_lastMoved = 0;
    ULONGLONG g_lastRun = 0;
    uintptr_t g_pickupClass = 0;
    char g_status[192] = "Loot magnet ready.";

    bool SetActorLocation(uintptr_t actor, const FVector& location)
    {
        if (!actor || !location.IsFinite())
            return false;
        alignas(16) std::array<uint8_t, 0x128> params{};
        std::memcpy(params.data(), &location, sizeof(location));
        params[0x18] = 0;   // bSweep
        params[0x120] = 1;  // bTeleport
        const bool dispatched = GameAccess::InvokeFunctionRaw(actor,
            FunctionIndices::Actor_K2_SetActorLocation,
            params.data(), params.size());
        return dispatched && params[0x121] != 0;
    }

    uintptr_t PickupClass()
    {
        if (!g_pickupClass)
            g_pickupClass = GameAccess::GetObjectByIndex(ObjectIndices::PickUpActorClass);
        return g_pickupClass;
    }

    FVector MagnetDestination(int ordinal)
    {
        auto camera = GameAccess::GetRenderCamera();
        if (!camera.Valid)
            camera = GameAccess::GetCamera();

        FVector base{};
        if (!GameAccess::GetActorLocation(GameAccess::GetLocalPawn(), base))
            base = camera.Location;

        constexpr double DegToRad = 3.14159265358979323846 / 180.0;
        const double yaw = camera.Rotation.Yaw * DegToRad;
        const double forwardCm = static_cast<double>(g_dropDistanceMeters) * 100.0;
        const double heightCm = static_cast<double>(g_dropHeightMeters) * 100.0;

        // Spread the pile slightly so physics pickups do not all occupy the same point.
        constexpr double GoldenAngle = 2.39996322972865332;
        const double ring = 12.0 + 5.0 * static_cast<double>(ordinal % 6);
        const double angle = GoldenAngle * static_cast<double>(ordinal);
        return base + FVector(
            std::cos(yaw) * forwardCm + std::cos(angle) * ring,
            std::sin(yaw) * forwardCm + std::sin(angle) * ring,
            heightCm + static_cast<double>(ordinal % 3) * 5.0);
    }

    void RunMagnet(bool manual)
    {
        g_lastMatched = 0;
        g_lastMoved = 0;
        const uintptr_t pawn = GameAccess::GetLocalPawn();
        const uintptr_t world = GameAccess::GetWorld();
        const uintptr_t pickupClass = PickupClass();
        if (!pawn || !world || !pickupClass)
        {
            std::snprintf(g_status, sizeof(g_status),
                "Loot magnet: waiting for pawn/world/PickUpActor class");
            return;
        }

        FVector local{};
        if (!GameAccess::GetActorLocation(pawn, local))
        {
            std::snprintf(g_status, sizeof(g_status), "Loot magnet: local position unavailable");
            return;
        }

        const auto& actors = GameAccess::GetActors();
        const int limit = std::clamp(g_maxPerPass, 1, 64);
        const double maxCm = static_cast<double>(std::clamp(g_rangeMeters, 2.0f, 150.0f)) * 100.0;
        const double minCm = 140.0; // items already at our feet are left alone

        int ordinal = 0;
        for (const uintptr_t actor : actors)
        {
            if (!actor || actor == pawn || !GameAccess::IsInstanceOf(actor, pickupClass))
                continue;
            FVector location{};
            if (!GameAccess::GetActorLocation(actor, location))
                continue;
            const double distance = local.Distance(location);
            if (!std::isfinite(distance) || distance > maxCm)
                continue;
            ++g_lastMatched;
            if (distance <= minCm || g_lastMoved >= limit)
                continue;
            if (SetActorLocation(actor, MagnetDestination(ordinal++)))
                ++g_lastMoved;
        }

        std::snprintf(g_status, sizeof(g_status),
            "Loot magnet: %d pickup actors matched, %d moved%s",
            g_lastMatched, g_lastMoved, manual ? " (manual)" : "");
    }
}

namespace ItemMagnet
{
    bool enabled = false;
    int selectedIndex = 0; // retained for source compatibility with the original menu API

    void ProcessTick()
    {
        if (!enabled)
            return;
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG interval = static_cast<ULONGLONG>(std::clamp(g_intervalMs, 100, 1000));
        if (g_lastRun && now - g_lastRun < interval)
            return;
        g_lastRun = now;
        RunMagnet(false);
    }

    void Shutdown()
    {
        enabled = false;
        g_lastRun = 0;
        g_pickupClass = 0;
        g_lastMatched = 0;
        g_lastMoved = 0;
    }

    void PullAllItems()
    {
        RunMagnet(true);
    }

    // The current dump exposes the real PickUpActor class but no cheap, authoritative
    // per-world value field. Keep these legacy entry points safe rather than guessing.
    void PullHighValueItems()
    {
        RunMagnet(true);
    }

    void PullSelectedItem()
    {
        RunMagnet(true);
    }

    void RenderTab()
    {
        const uintptr_t pickupClass = PickupClass();
        ImGui::Text("Loot Magnet | PickUpActor class: %s | cached actors: %d",
            pickupClass ? "VALID" : "WAIT",
            static_cast<int>(GameAccess::GetActors().size()));
        ImGui::TextWrapped("Uses the fresh dump's real Test_C.PickUpActor UClass and the validated AActor::K2_SetActorLocation function. It reuses GameAccess's existing actor cache; there is no separate UObject or level scan.");

        ImGui::Checkbox("Continuous loot magnet", &enabled);
        ImGui::SliderFloat("Pull range", &g_rangeMeters, 5.0f, 150.0f, "%.0f m");
        ImGui::SliderFloat("Drop distance", &g_dropDistanceMeters, 0.8f, 5.0f, "%.1f m");
        ImGui::SliderFloat("Drop height", &g_dropHeightMeters, 0.0f, 2.0f, "%.1f m");
        ImGui::SliderInt("Max pickups per pass", &g_maxPerPass, 1, 64);
        ImGui::SliderInt("Magnet interval", &g_intervalMs, 100, 1000, "%d ms");

        if (ImGui::Button("Pull nearby loot now"))
            PullAllItems();
        ImGui::SameLine();
        if (ImGui::Button("Performance preset"))
        {
            g_rangeMeters = 30.0f;
            g_maxPerPass = 16;
            g_intervalMs = 350;
        }
        ImGui::SameLine();
        if (ImGui::Button("Aggressive preset"))
        {
            g_rangeMeters = 100.0f;
            g_maxPerPass = 48;
            g_intervalMs = 150;
        }

        ImGui::Text("Last pass: matched %d | moved %d", g_lastMatched, g_lastMoved);
        ImGui::TextWrapped("%s", g_status);
        ImGui::TextDisabled("Continuous mode is opt-in and hard-capped at 64 teleports per pass. Pickups already within 1.4 m are ignored to prevent jitter.");
    }

    void Render()
    {
        RenderTab();
    }
}
