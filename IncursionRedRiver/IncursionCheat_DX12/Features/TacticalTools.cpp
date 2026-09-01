#include "TacticalTools.h"

#include "Aimbot.h"
#include "../sdk/GameAccess.h"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    struct Blip
    {
        uintptr_t Actor = 0;
        FVector Location{};
        float DistanceMeters = 0.0f;
        bool Hostile = false;
    };

    std::vector<Blip> g_blips;
    ULONGLONG g_lastRefresh = 0;
    uintptr_t g_snapshotWorld = 0;
    FVector g_localPosition{};
    float g_nearestHostile = 0.0f;
    int g_hostileCount = 0;

    bool g_radar = false;
    bool g_edgeIndicators = false;
    bool g_proximityWarning = false;
    bool g_targetLine = false;
    float g_radarRange = 150.0f;
    float g_radarSize = 190.0f;
    float g_edgeRange = 300.0f;
    float g_warningDistance = 20.0f;
    int g_corner = 1; // top-right
    int g_refreshMs = 100;

    void RefreshSnapshot()
    {
        const uintptr_t world = GameAccess::GetWorld();
        if (world != g_snapshotWorld)
        {
            g_snapshotWorld = world;
            g_blips.clear();
        }

        const uintptr_t pawn = GameAccess::GetLocalPawn();
        auto camera = GameAccess::GetRenderCamera();
        if (!camera.Valid)
            camera = GameAccess::GetCamera();
        if (!camera.Valid)
            return;

        g_localPosition = camera.Location;
        if (pawn)
            GameAccess::GetActorLocation(pawn, g_localPosition);

        g_blips.clear();
        g_hostileCount = 0;
        g_nearestHostile = 0.0f;
        const auto& diagnostics = GameAccess::GetDiagnostics();
        const auto& characters = GameAccess::GetCharacters();
        g_blips.reserve(std::min<size_t>(characters.size(), 128));

        for (const uintptr_t actor : characters)
        {
            if (!actor || actor == pawn || !GameAccess::IsLivingCharacter(actor))
                continue;
            const bool hostile = GameAccess::IsEnemyCharacter(actor);
            if (diagnostics.HostileArrayValid && !hostile)
                continue;
            FVector position{};
            if (!GameAccess::GetActorLocation(actor, position))
                continue;
            const float distance = static_cast<float>(g_localPosition.Distance(position) / 100.0);
            if (distance > std::max(g_edgeRange, g_radarRange) * 1.25f)
                continue;
            g_blips.push_back({ actor, position, distance, hostile });
            if (hostile)
            {
                ++g_hostileCount;
                if (g_nearestHostile <= 0.0f || distance < g_nearestHostile)
                    g_nearestHostile = distance;
            }
        }

        std::sort(g_blips.begin(), g_blips.end(),
            [](const Blip& a, const Blip& b) { return a.DistanceMeters < b.DistanceMeters; });
        if (g_blips.size() > 128)
            g_blips.resize(128);
    }

    ImVec2 RadarTopLeft(const ImVec2& display)
    {
        constexpr float margin = 18.0f;
        switch (g_corner)
        {
        case 0: return { margin, margin };
        case 1: return { std::max(margin, display.x - g_radarSize - margin), margin };
        case 2: return { margin, std::max(margin, display.y - g_radarSize - margin) };
        default:return { std::max(margin, display.x - g_radarSize - margin),
                         std::max(margin, display.y - g_radarSize - margin) };
        }
    }

    void DrawRadar(const GameAccess::Camera& camera)
    {
        if (!g_radar || !ImGui::GetCurrentContext())
            return;
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x <= 0.0f || display.y <= 0.0f)
            return;
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        const ImVec2 topLeft = RadarTopLeft(display);
        const ImVec2 bottomRight(topLeft.x + g_radarSize, topLeft.y + g_radarSize);
        const ImVec2 center(topLeft.x + g_radarSize * 0.5f, topLeft.y + g_radarSize * 0.5f);
        const float radius = g_radarSize * 0.5f - 8.0f;

        draw->AddRectFilled(topLeft, bottomRight, IM_COL32(8, 10, 13, 185), 7.0f);
        draw->AddRect(topLeft, bottomRight, IM_COL32(220, 225, 235, 170), 7.0f, 0, 1.0f);
        draw->AddCircle(center, radius, IM_COL32(140, 145, 155, 150), 48, 1.0f);
        draw->AddCircle(center, radius * 0.5f, IM_COL32(90, 95, 105, 120), 48, 1.0f);
        draw->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y),
                      IM_COL32(80, 85, 95, 100), 1.0f);
        draw->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius),
                      IM_COL32(80, 85, 95, 100), 1.0f);
        draw->AddCircleFilled(center, 3.5f, IM_COL32(110, 210, 255, 255));

        constexpr double DegToRad = 3.14159265358979323846 / 180.0;
        const double yaw = camera.Rotation.Yaw * DegToRad;
        const double cy = std::cos(yaw);
        const double sy = std::sin(yaw);
        const uintptr_t aimTarget = Aimbot::GetDiagnostics().TargetActor;

        for (const auto& blip : g_blips)
        {
            if (blip.DistanceMeters > g_radarRange)
                continue;
            const double dx = blip.Location.X - g_localPosition.X;
            const double dy = blip.Location.Y - g_localPosition.Y;
            const double forward = cy * dx + sy * dy;
            const double right = -sy * dx + cy * dy;
            const float normalizedRight = static_cast<float>(right / (g_radarRange * 100.0));
            const float normalizedForward = static_cast<float>(forward / (g_radarRange * 100.0));
            const float length = std::hypot(normalizedRight, normalizedForward);
            const float scale = length > 1.0f ? 1.0f / length : 1.0f;
            const ImVec2 p(center.x + normalizedRight * scale * radius,
                           center.y - normalizedForward * scale * radius);
            const bool targeted = blip.Actor == aimTarget;
            const ImU32 color = targeted ? IM_COL32(255, 210, 70, 255) :
                (blip.Hostile ? IM_COL32(255, 75, 75, 245) : IM_COL32(255, 175, 60, 210));
            draw->AddCircleFilled(p, targeted ? 5.0f : 3.5f, color);
        }

        char label[96]{};
        std::snprintf(label, sizeof(label), "RADAR %.0fm | hostile %d | nearest %.1fm",
            g_radarRange, g_hostileCount, g_nearestHostile);
        draw->AddText(ImVec2(topLeft.x + 7.0f, bottomRight.y + 3.0f),
                      IM_COL32(240, 240, 245, 230), label);
    }

    void DrawEdgeIndicators(const GameAccess::Camera& camera)
    {
        if (!g_edgeIndicators || !ImGui::GetCurrentContext())
            return;
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x <= 0.0f || display.y <= 0.0f)
            return;
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
        const float rx = display.x * 0.43f;
        const float ry = display.y * 0.40f;
        constexpr double DegToRad = 3.14159265358979323846 / 180.0;
        const double yaw = camera.Rotation.Yaw * DegToRad;
        const uintptr_t aimTarget = Aimbot::GetDiagnostics().TargetActor;

        for (const auto& blip : g_blips)
        {
            if (!blip.Hostile || blip.DistanceMeters > g_edgeRange)
                continue;
            const double dx = blip.Location.X - g_localPosition.X;
            const double dy = blip.Location.Y - g_localPosition.Y;
            const double worldAngle = std::atan2(dy, dx);
            const double rel = worldAngle - yaw;
            const float sx = static_cast<float>(std::sin(rel));
            const float sy = static_cast<float>(-std::cos(rel));
            const ImVec2 p(center.x + sx * rx, center.y + sy * ry);
            const float len = std::max(std::hypot(sx, sy), 0.001f);
            const float nx = sx / len;
            const float ny = sy / len;
            const float tx = -ny;
            const float ty = nx;
            const float size = blip.Actor == aimTarget ? 12.0f : 9.0f;
            const ImVec2 tip(p.x + nx * size, p.y + ny * size);
            const ImVec2 left(p.x - nx * size * 0.55f + tx * size * 0.7f,
                              p.y - ny * size * 0.55f + ty * size * 0.7f);
            const ImVec2 right(p.x - nx * size * 0.55f - tx * size * 0.7f,
                               p.y - ny * size * 0.55f - ty * size * 0.7f);
            const ImU32 color = blip.Actor == aimTarget ? IM_COL32(255, 210, 70, 245) :
                                                         IM_COL32(255, 75, 75, 225);
            draw->AddTriangleFilled(tip, left, right, color);
        }
    }

    void DrawWarnings()
    {
        if (!g_proximityWarning || g_nearestHostile <= 0.0f ||
            g_nearestHostile > g_warningDistance || !ImGui::GetCurrentContext())
            return;
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        char text[96]{};
        std::snprintf(text, sizeof(text), "HOSTILE %.1f m", g_nearestHostile);
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 pos(display.x * 0.5f - size.x * 0.5f, 65.0f);
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        draw->AddRectFilled(ImVec2(pos.x - 10.0f, pos.y - 6.0f),
                            ImVec2(pos.x + size.x + 10.0f, pos.y + size.y + 6.0f),
                            IM_COL32(45, 5, 5, 190), 5.0f);
        draw->AddText(pos, IM_COL32(255, 95, 95, 255), text);
    }

    void DrawTargetLine()
    {
        if (!g_targetLine || !ImGui::GetCurrentContext())
            return;
        const uintptr_t target = Aimbot::GetDiagnostics().TargetActor;
        if (!target)
            return;
        FVector point{};
        if (!GameAccess::GetPoseAwareBodyTarget(target, "chest", point) &&
            !GameAccess::GetActorLocation(target, point))
            return;
        Vector2 screen{};
        if (!GameAccess::ProjectWorldToScreen(point, screen))
            return;
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::GetForegroundDrawList()->AddLine(
            ImVec2(display.x * 0.5f, display.y),
            ImVec2(screen.x, screen.y), IM_COL32(255, 210, 70, 190), 1.25f);
    }
}

namespace TacticalTools
{
    void ProcessFrame()
    {
        if (!g_radar && !g_edgeIndicators && !g_proximityWarning && !g_targetLine)
            return;
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG interval = static_cast<ULONGLONG>(std::clamp(g_refreshMs, 50, 500));
        if (!g_lastRefresh || now - g_lastRefresh >= interval)
        {
            RefreshSnapshot();
            g_lastRefresh = now;
        }

        auto camera = GameAccess::GetRenderCamera();
        if (!camera.Valid)
            camera = GameAccess::GetCamera();
        if (!camera.Valid)
            return;
        DrawRadar(camera);
        DrawEdgeIndicators(camera);
        DrawWarnings();
        DrawTargetLine();
    }

    void Shutdown()
    {
        g_blips.clear();
        g_lastRefresh = 0;
        g_snapshotWorld = 0;
    }

    void RenderTab()
    {
        ImGui::Text("Tactical overlay | cached hostiles: %d | nearest: %.1f m",
            g_hostileCount, g_nearestHostile);
        ImGui::TextWrapped("This is separate from the working ESP. It reuses GameAccess's 30 Hz actor cache and refreshes its own lightweight position snapshot at 10 Hz by default.");

        ImGui::Checkbox("Radar", &g_radar);
        if (g_radar)
        {
            ImGui::SliderFloat("Radar range", &g_radarRange, 25.0f, 500.0f, "%.0f m");
            ImGui::SliderFloat("Radar size", &g_radarSize, 120.0f, 320.0f, "%.0f px");
            const char* corners[] = { "Top-left", "Top-right", "Bottom-left", "Bottom-right" };
            ImGui::Combo("Radar corner", &g_corner, corners, 4);
        }

        ImGui::Checkbox("Off-screen hostile indicators", &g_edgeIndicators);
        if (g_edgeIndicators)
            ImGui::SliderFloat("Indicator range", &g_edgeRange, 25.0f, 1000.0f, "%.0f m");

        ImGui::Checkbox("Proximity warning", &g_proximityWarning);
        if (g_proximityWarning)
            ImGui::SliderFloat("Warning distance", &g_warningDistance, 5.0f, 100.0f, "%.0f m");

        ImGui::Checkbox("Line to current aimbot target", &g_targetLine);
        ImGui::SliderInt("Tactical refresh interval", &g_refreshMs, 50, 500, "%d ms");
        ImGui::TextDisabled("Radar/edge indicators perform no LineOfSight ProcessEvent calls. Aimbot visibility remains the only LOS-heavy path.");
    }
}
