#include "ESP.h"

#include "../Menu.h"
#include "../sdk/GameAccess.h"
#include "../utils/Renderer.h"
#include "../utils/WorldToScreen.h"
#include "../imgui/imgui.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    int g_lastCandidateCount = 0;
    int g_lastRenderedCount = 0;
    int g_lastVisibleCount = 0;
    int g_lastHiddenCount = 0;
    uintptr_t g_nameCacheWorld = 0;
    std::unordered_map<uintptr_t, std::string> g_nameCache;

    struct ScreenTrack
    {
        Vector2 Top{};
        Vector2 Bottom{};
        ULONGLONG LastSeen = 0;
        bool Valid = false;
    };

    std::unordered_map<uintptr_t, ScreenTrack> g_screenTracks;

    struct Candidate
    {
        uintptr_t Actor = 0;
        FVector Location{};
        float DistanceMeters = 0.0f;
        float HalfHeight = 88.0f;
        bool Hostile = false;
        bool OnScreen = false;
    };

    std::string Narrow(const std::wstring& input)
    {
        if (input.empty())
            return {};
        const int needed = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
            return {};
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), out.data(), needed, nullptr, nullptr);
        return out;
    }

    void StabilizeScreenBox(uintptr_t actor, const Vector2& rawTop,
                            const Vector2& rawBottom, Vector2& top,
                            Vector2& bottom)
    {
        auto& track = g_screenTracks[actor];
        const ULONGLONG now = GetTickCount64();
        if (!track.Valid || !track.LastSeen || now - track.LastSeen > 250)
        {
            track.Top = rawTop;
            track.Bottom = rawBottom;
            track.Valid = true;
        }
        else
        {
            const float oldCenterX = (track.Top.x + track.Bottom.x) * 0.5f;
            const float oldCenterY = (track.Top.y + track.Bottom.y) * 0.5f;
            const float newCenterX = (rawTop.x + rawBottom.x) * 0.5f;
            const float newCenterY = (rawTop.y + rawBottom.y) * 0.5f;
            const float delta = std::hypot(newCenterX - oldCenterX,
                                           newCenterY - oldCenterY);
            // Suppress only tiny game/render-thread timing noise. Real camera turns
            // and actor movement use a high response so the box cannot trail behind.
            const float alpha = delta < 6.0f ? 0.80f :
                                (delta < 20.0f ? 0.95f : 1.0f);
            track.Top.x += (rawTop.x - track.Top.x) * alpha;
            track.Top.y += (rawTop.y - track.Top.y) * alpha;
            track.Bottom.x += (rawBottom.x - track.Bottom.x) * alpha;
            track.Bottom.y += (rawBottom.y - track.Bottom.y) * alpha;
        }
        track.LastSeen = now;
        top = track.Top;
        bottom = track.Bottom;
    }

}

namespace ESP
{
    bool bEnabled = true;
    bool bDrawBoxes = true;
    bool bShowHealth = true;
    bool bShowNames = true;
    bool bShowDistance = true;
    float maxDistanceMeters = 150.0f;
    int maxActors = 128;

    void Init() {}
    void Enable() { bEnabled = true; }
    void Disable() { bEnabled = false; }
    void Toggle() { bEnabled = !bEnabled; }
    bool IsEnabled() { return bEnabled; }

    void Run()
    {
        g_lastCandidateCount = 0;
        g_lastRenderedCount = 0;
        g_lastVisibleCount = 0;
        g_lastHiddenCount = 0;

        // The menu owns the foreground draw list while open. Skipping the world
        // overlay here removes redundant projection, name-cache and visibility
        // work during menu interaction without changing the menu itself.
        if (!bEnabled || Menu::bOpen || !ImGui::GetCurrentContext())
            return;

        const uintptr_t localPawn = GameAccess::GetLocalPawn();
        // Present draws over the backbuffer that was rendered with the previous game
        // camera cache. Sampling CameraCachePrivate or invoking projection here reads
        // a newer game-thread frame and makes ESP swim during local movement.
        const auto camera = GameAccess::GetRenderCamera();
        if (!camera.Valid)
            return;

        FVector localPos = camera.Location;
        if (localPawn)
            GameAccess::GetActorLocation(localPawn, localPos);

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const int width = static_cast<int>(display.x);
        const int height = static_cast<int>(display.y);
        if (width <= 0 || height <= 0)
            return;

        const uintptr_t world = GameAccess::GetWorld();
        if (world != g_nameCacheWorld)
        {
            g_nameCacheWorld = world;
            g_nameCache.clear();
            g_screenTracks.clear();
        }

        std::vector<Candidate> candidates;
        candidates.reserve(GameAccess::GetCharacters().size());
        for (const uintptr_t actor : GameAccess::GetCharacters())
        {
            if (actor == localPawn)
                continue;
            const bool confirmedHostile = GameAccess::IsEnemyCharacter(actor);
            // Only confirmed hostiles are rendered. Neutral/unknown IRR actors
            // are intentionally omitted instead of being mislabeled as Enemy
            // when the transient team list is unavailable.
            if (!confirmedHostile)
                continue;

            FVector location{};
            if (!GameAccess::GetActorLocation(actor, location))
                continue;

            const float distanceMeters = static_cast<float>(localPos.Distance(location) / 100.0);
            if (distanceMeters > maxDistanceMeters)
                continue;

            Vector2 centerScreen{};
            const bool projected = WorldToScreen::Convert(location, centerScreen,
                camera.Location, camera.Rotation, camera.FOV, width, height);
            const bool onScreen = projected && centerScreen.x >= -80.0f &&
                centerScreen.x <= display.x + 80.0f && centerScreen.y >= -80.0f &&
                centerScreen.y <= display.y + 80.0f;
            candidates.push_back({ actor, location, distanceMeters,
                                   GameAccess::GetCapsuleHalfHeight(actor),
                                   confirmedHostile, onScreen });
        }

        g_lastCandidateCount = static_cast<int>(candidates.size());
        std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b)
            {
                if (a.OnScreen != b.OnScreen)
                    return a.OnScreen > b.OnScreen;
                return a.DistanceMeters < b.DistanceMeters;
            });

        // Prioritize hostile actors that are actually on screen instead of taking
        // the nearest N actors (which can spend the entire async budget on enemies
        // behind the camera). This keeps red/green state responsive for every
        // visible target while projection remains a cheap local math operation.
        struct VisibilityPriority { uintptr_t Actor = 0; float Score = 0.0f; };
        std::vector<VisibilityPriority> priorities;
        priorities.reserve(candidates.size());
        for (const Candidate& candidate : candidates)
        {
            if (!candidate.Hostile)
                continue;
            Vector2 screen{};
            const bool projected = WorldToScreen::Convert(candidate.Location, screen,
                camera.Location, camera.Rotation, camera.FOV, width, height);
            const bool onScreen = projected && screen.x >= -80.0f &&
                screen.x <= display.x + 80.0f && screen.y >= -80.0f &&
                screen.y <= display.y + 80.0f;
            const float score = onScreen ?
                std::hypot(screen.x - display.x * 0.5f,
                           screen.y - display.y * 0.5f) : 1000000000.0f;
            priorities.push_back({ candidate.Actor, score });
        }
        std::sort(priorities.begin(), priorities.end(),
            [](const VisibilityPriority& left, const VisibilityPriority& right)
            { return left.Score < right.Score; });
        std::vector<uintptr_t> visibilityActors;
        visibilityActors.reserve(priorities.size());
        for (const VisibilityPriority& priority : priorities)
            visibilityActors.push_back(priority.Actor);
        GameAccess::RequestVisibilitySamples(visibilityActors, "chest", 120);

        const int renderLimit = std::max(maxActors, 1);
        for (const Candidate& candidate : candidates)
        {
            if (g_lastRenderedCount >= renderLimit)
                break;

            const FVector topWorld = candidate.Location + FVector(
                0.0, 0.0, static_cast<double>(candidate.HalfHeight));
            const FVector bottomWorld = candidate.Location - FVector(
                0.0, 0.0, static_cast<double>(candidate.HalfHeight));

            Vector2 rawTop{}, rawBottom{};
            if (!WorldToScreen::Convert(topWorld, rawTop, camera.Location, camera.Rotation,
                                        camera.FOV, width, height) ||
                !WorldToScreen::Convert(bottomWorld, rawBottom, camera.Location,
                                        camera.Rotation, camera.FOV, width, height))
                continue;

            Vector2 top{}, bottom{};
            StabilizeScreenBox(candidate.Actor, rawTop, rawBottom, top, bottom);

            const float boxHeight = std::abs(bottom.y - top.y);
            if (boxHeight < 2.0f || boxHeight > display.y * 2.0f)
                continue;
            const float boxWidth = boxHeight * 0.42f;
            const float left = top.x - boxWidth * 0.5f;
            const float topY = std::min(top.y, bottom.y);
            if (left + boxWidth < 0.0f || left > display.x ||
                topY + boxHeight < 0.0f || topY > display.y)
                continue;

            const auto health = GameAccess::GetHealth(candidate.Actor);
            if (health.Valid && (health.Dead || health.Current <= health.Minimum))
                continue;

            ++g_lastRenderedCount;
            bool visible = false;
            const bool visibilityKnown = candidate.Hostile &&
                GameAccess::GetCachedVisibility(candidate.Actor, visible,
                                                nullptr, 800);
            if (candidate.Hostile)
            {
                if (visibilityKnown && visible)
                    ++g_lastVisibleCount;
                else
                    ++g_lastHiddenCount;
            }
            const ImU32 color = !candidate.Hostile ? IM_COL32(255, 190, 70, 220) :
                (visibilityKnown && visible ? IM_COL32(70, 235, 120, 235) :
                                              IM_COL32(255, 70, 70, 235));
            if (bDrawBoxes)
                Renderer::DrawBox(left, topY, boxWidth, boxHeight, color, 1.5f);

            if (bShowHealth && health.Valid)
            {
                const float maxHealth = health.Maximum > 0.0f ? health.Maximum : 1.0f;
                Renderer::DrawHealthBar(left, topY - 7.0f,
                                        boxWidth, 4.0f,
                                        std::clamp(health.Current, 0.0f, maxHealth), maxHealth);
            }

            float textY = topY - 22.0f;
            if (bShowNames)
            {
                auto cached = g_nameCache.find(candidate.Actor);
                if (cached == g_nameCache.end())
                    cached = g_nameCache.emplace(candidate.Actor,
                        Narrow(GameAccess::GetPlayerName(candidate.Actor))).first;
                std::string name = cached->second;
                if (name.empty())
                    name = candidate.Hostile ? "Enemy" : "IRR candidate (team unknown)";
                Renderer::DrawText(left, textY, name.c_str(),
                                   IM_COL32(255, 255, 255, 235));
                textY -= 14.0f;
            }

            if (bShowDistance)
            {
                char distanceText[64]{};
                std::snprintf(distanceText, sizeof(distanceText), "%.1f m",
                              candidate.DistanceMeters);
                Renderer::DrawText(left,
                                   topY + boxHeight + 3.0f,
                                   distanceText, IM_COL32(235, 235, 235, 220));
            }
        }

        const ULONGLONG now = GetTickCount64();
        for (auto it = g_screenTracks.begin(); it != g_screenTracks.end();)
        {
            if (now - it->second.LastSeen > 2000)
                it = g_screenTracks.erase(it);
            else
                ++it;
        }
    }

    void Render()
    {
        ImGui::Checkbox("Enable ESP", &bEnabled);
        ImGui::Checkbox("Boxes", &bDrawBoxes);
        ImGui::Checkbox("Health", &bShowHealth);
        ImGui::Checkbox("Names", &bShowNames);
        ImGui::Checkbox("Distance", &bShowDistance);
        ImGui::SliderFloat("Max Distance", &maxDistanceMeters, 25.0f, 1000.0f, "%.0f m");
        ImGui::SliderInt("Max On-screen Actors", &maxActors, 5, 256);
        ImGui::Text("Candidates in range: %d | rendered: %d",
            g_lastCandidateCount, g_lastRenderedCount);
        ImGui::Text("Exposure: %d visible (green) | %d hidden/pending (red)",
            g_lastVisibleCount, g_lastHiddenCount);
        ImGui::TextWrapped("ESP is projected from LastFrameCameraCachePrivate, matching the backbuffer being presented instead of a newer game-thread camera. A small adaptive filter removes sub-frame jitter but responds immediately to real turns and target movement. No per-actor ProcessEvent projection calls are made.");
        ImGui::TextWrapped("Actor discovery is based on actual IRRBaseCharacter class identity and remains independent of local-pawn acquisition; health is a secondary living/dead filter.");
        ImGui::TextWrapped("Confirmed hostiles share the aimbot's budgeted multi-point exposure cache: green means at least one live body anchor is visible, while red means fully occluded or awaiting a fresh sample. Amber candidates are shown only while the team list is unavailable.");
    }
}
