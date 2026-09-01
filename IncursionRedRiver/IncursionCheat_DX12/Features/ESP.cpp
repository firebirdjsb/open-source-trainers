#include "ESP.h"

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
    int g_lastSkeletonActors = 0;
    int g_lastSkeletonPoints = 0;
    int g_lastCandidateCount = 0;
    int g_lastRenderedCount = 0;
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
            const float alpha = delta < 6.0f ? 0.22f :
                                (delta < 20.0f ? 0.62f : 0.92f);
            track.Top.x += (rawTop.x - track.Top.x) * alpha;
            track.Top.y += (rawTop.y - track.Top.y) * alpha;
            track.Bottom.x += (rawBottom.x - track.Bottom.x) * alpha;
            track.Bottom.y += (rawBottom.y - track.Bottom.y) * alpha;
        }
        track.LastSeen = now;
        top = track.Top;
        bottom = track.Bottom;
    }

    void DrawBoneSkeleton(const std::vector<GameAccess::BonePoint>& bones,
                          const GameAccess::Camera& camera,
                          int width, int height)
    {
        if (bones.size() < 2)
            return;

        struct ProjectedBone
        {
            Vector2 Screen{};
            bool Visible = false;
        };

        std::vector<ProjectedBone> projected(bones.size());
        std::unordered_map<int32_t, size_t> byIndex;
        int visiblePoints = 0;
        for (size_t i = 0; i < bones.size(); ++i)
        {
            byIndex[bones[i].Index] = i;
            projected[i].Visible = WorldToScreen::Convert(
                bones[i].World, projected[i].Screen,
                camera.Location, camera.Rotation, camera.FOV, width, height);
            if (projected[i].Visible)
                ++visiblePoints;
        }
        if (visiblePoints < 2)
            return;

        ++g_lastSkeletonActors;
        g_lastSkeletonPoints += visiblePoints;
        const ImU32 boneColor = IM_COL32(255, 205, 80, 225);

        // Prefer the verified native reference-skeleton parent topology. If Dumper-7
        // cannot prove that native array for a particular mesh, fall back to an
        // anatomical chain selected from the *real cached bone transforms*. This keeps
        // Bone ESP useful without inventing world positions or reading guessed offsets.
        int parentEdges = 0;
        for (size_t i = 0; i < bones.size(); ++i)
        {
            if (!projected[i].Visible)
                continue;
            const auto parent = byIndex.find(bones[i].ParentIndex);
            if (parent != byIndex.end() && projected[parent->second].Visible)
            {
                Renderer::DrawLine(projected[i].Screen.x, projected[i].Screen.y,
                                   projected[parent->second].Screen.x,
                                   projected[parent->second].Screen.y, boneColor, 1.25f);
                ++parentEdges;
            }
        }

        if (parentEdges == 0)
        {
            double minZ = 1.0e30;
            double maxZ = -1.0e30;
            for (size_t i = 0; i < bones.size(); ++i)
            {
                if (!projected[i].Visible)
                    continue;
                minZ = std::min(minZ, bones[i].World.Z);
                maxZ = std::max(maxZ, bones[i].World.Z);
            }
            const double heightSpan = maxZ - minZ;
            if (std::isfinite(heightSpan) && heightSpan > 40.0 && heightSpan < 350.0)
            {
                auto centralAt = [&](double normalizedZ) -> int
                {
                    int best = -1;
                    double bestScore = 1.0e30;
                    const double targetZ = minZ + heightSpan * normalizedZ;
                    for (size_t i = 0; i < bones.size(); ++i)
                    {
                        if (!projected[i].Visible)
                            continue;
                        const double lateral = std::abs(bones[i].Component.Y);
                        const double score = std::abs(bones[i].World.Z - targetZ) * 3.0 +
                                             lateral * 0.20;
                        if (score < bestScore)
                        {
                            bestScore = score;
                            best = static_cast<int>(i);
                        }
                    }
                    return best;
                };

                auto lateralAt = [&](double minNorm, double maxNorm, bool positive,
                                     bool farthest) -> int
                {
                    int best = -1;
                    double metric = farthest ?
                        -1.0 : 1.0e30;
                    for (size_t i = 0; i < bones.size(); ++i)
                    {
                        if (!projected[i].Visible)
                            continue;
                        const double norm = (bones[i].World.Z - minZ) / heightSpan;
                        if (norm < minNorm || norm > maxNorm)
                            continue;
                        const double side = bones[i].Component.Y;
                        if ((positive && side < 0.0) || (!positive && side > 0.0))
                            continue;
                        const double magnitude = std::abs(side);
                        if ((farthest && magnitude > metric) ||
                            (!farthest && magnitude < metric))
                        {
                            metric = magnitude;
                            best = static_cast<int>(i);
                        }
                    }
                    return best;
                };

                const int head = centralAt(0.98);
                const int neck = centralAt(0.86);
                const int chest = centralAt(0.68);
                const int pelvis = centralAt(0.48);
                const int shoulderL = lateralAt(0.62, 0.82, true, true);
                const int shoulderR = lateralAt(0.62, 0.82, false, true);
                const int handL = lateralAt(0.42, 0.76, true, true);
                const int handR = lateralAt(0.42, 0.76, false, true);
                const int kneeL = lateralAt(0.18, 0.48, true, false);
                const int kneeR = lateralAt(0.18, 0.48, false, false);
                const int footL = lateralAt(0.00, 0.24, true, true);
                const int footR = lateralAt(0.00, 0.24, false, true);

                auto line = [&](int a, int b)
                {
                    if (a < 0 || b < 0 || a == b)
                        return;
                    Renderer::DrawLine(projected[static_cast<size_t>(a)].Screen.x,
                                       projected[static_cast<size_t>(a)].Screen.y,
                                       projected[static_cast<size_t>(b)].Screen.x,
                                       projected[static_cast<size_t>(b)].Screen.y,
                                       boneColor, 1.35f);
                };
                line(head, neck);
                line(neck, chest);
                line(chest, pelvis);
                line(chest, shoulderL);
                line(shoulderL, handL);
                line(chest, shoulderR);
                line(shoulderR, handR);
                line(pelvis, kneeL);
                line(kneeL, footL);
                line(pelvis, kneeR);
                line(kneeR, footR);
            }
        }
    }
}

namespace ESP
{
    bool bEnabled = true;
    bool bDrawBoxes = true;
    bool bDrawSkeleton = false;
    bool bShowHealth = true;
    bool bShowNames = true;
    bool bShowDistance = true;
    float maxDistanceMeters = 150.0f;
    float skeletonDistanceMeters = 75.0f;
    int maxActors = 25;

    void Init() {}
    void Enable() { bEnabled = true; }
    void Disable() { bEnabled = false; }
    void Toggle() { bEnabled = !bEnabled; }
    bool IsEnabled() { return bEnabled; }

    void Run()
    {
        g_lastSkeletonActors = 0;
        g_lastSkeletonPoints = 0;
        g_lastCandidateCount = 0;
        g_lastRenderedCount = 0;

        if (!bEnabled || !ImGui::GetCurrentContext())
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

        const auto& diagnostics = GameAccess::GetDiagnostics();
        std::vector<Candidate> candidates;
        candidates.reserve(GameAccess::GetCharacters().size());
        for (const uintptr_t actor : GameAccess::GetCharacters())
        {
            if (actor == localPawn)
                continue;
            const bool confirmedHostile = GameAccess::IsEnemyCharacter(actor);
            if (diagnostics.HostileArrayValid && !confirmedHostile)
                continue;

            FVector location{};
            if (!GameAccess::GetActorLocation(actor, location))
                continue;

            const float distanceMeters = static_cast<float>(localPos.Distance(location) / 100.0);
            if (distanceMeters > maxDistanceMeters)
                continue;

            candidates.push_back({ actor, location, distanceMeters,
                                   GameAccess::GetCapsuleHalfHeight(actor),
                                   confirmedHostile });
        }

        g_lastCandidateCount = static_cast<int>(candidates.size());
        std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b)
            { return a.DistanceMeters < b.DistanceMeters; });
        for (const Candidate& candidate : candidates)
        {
            if (g_lastRenderedCount >= std::max(maxActors, 1))
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
            if (bDrawSkeleton && candidate.DistanceMeters <= skeletonDistanceMeters)
                DrawBoneSkeleton(GameAccess::GetBonePoints(candidate.Actor), camera, width, height);

            const ImU32 color = candidate.Hostile ? IM_COL32(255, 90, 90, 230) :
                                                   IM_COL32(255, 190, 70, 220);
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
        ImGui::Checkbox("Skeleton / Bone ESP", &bDrawSkeleton);
        ImGui::Checkbox("Health", &bShowHealth);
        ImGui::Checkbox("Names", &bShowNames);
        ImGui::Checkbox("Distance", &bShowDistance);
        ImGui::SliderFloat("Max Distance", &maxDistanceMeters, 25.0f, 1000.0f, "%.0f m");
        ImGui::SliderInt("Max On-screen Actors", &maxActors, 5, 100);
        ImGui::Text("On-screen candidates: %d | rendered: %d",
            g_lastCandidateCount, g_lastRenderedCount);
        if (bDrawSkeleton)
        {
            ImGui::SliderFloat("Skeleton Distance", &skeletonDistanceMeters,
                               10.0f, 250.0f, "%.0f m");
            const auto& d = GameAccess::GetDiagnostics();
            ImGui::Text("Bone cache: %d actors / %d points (independent)",
                d.ValidatedBoneActorCount, d.ValidatedBonePointCount);
            ImGui::Text("Projected this frame: %d actors / %d visible points",
                g_lastSkeletonActors, g_lastSkeletonPoints);
        }
        ImGui::TextWrapped("ESP is projected from LastFrameCameraCachePrivate, matching the backbuffer being presented instead of a newer game-thread camera. A small adaptive filter removes sub-frame jitter but responds immediately to real turns and target movement. No per-actor ProcessEvent projection calls are made.");
        ImGui::TextWrapped("Bone ESP reads and validates CachedComponentSpaceTransforms, then uses native reference-skeleton parent indices when the runtime layout can be proven.");
        ImGui::TextWrapped("Actor discovery is based on actual IRRBaseCharacter class identity and remains independent of local-pawn acquisition; health is a secondary living/dead filter.");
        ImGui::TextWrapped("Red actors are confirmed by the local IRRTeamComponent Hostiles array. Amber candidates are shown only while that team list is unavailable, so acquisition and bone debugging can continue without mislabeling them as enemies.");
    }
}
