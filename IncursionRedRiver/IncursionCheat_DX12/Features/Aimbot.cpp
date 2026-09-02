#include "Aimbot.h"

#include "../Memory/Memory.h"
#include "../Menu.h"
#include "../sdk/GameAccess.h"
#include "../sdk/Offsets.h"
#include "../hooks/PresentHook.h"
#include "../utils/Renderer.h"
#include "../utils/WorldToScreen.h"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr double Pi = 3.14159265358979323846;
    Aimbot::Diagnostics g_aimDiagnostics{};
    uintptr_t g_lockedActor = 0;
    bool g_rmbWasHeld = false;

    struct VisibilitySample
    {
        bool Visible = false;
        ULONGLONG SampledAt = 0;
    };

    std::unordered_map<uintptr_t, VisibilitySample> g_visibilityCache;
    std::mutex g_visibilityMutex;
    std::atomic<bool> g_visibilityTaskPending{ false };
    std::atomic<uint32_t> g_visibilitySampleThreadId{ 0 };
    ULONGLONG g_lastVisibilityRequestAt = 0;
    uintptr_t g_visibilityWorld = 0;

    struct AimCandidate
    {
        uintptr_t Actor = 0;
        FVector Target{};
        Vector2 Screen{};
        float ScreenDistance = 0.0f;
        bool CapsuleFallback = false;
        bool PoseAware = false;
        uintptr_t BodyComponent = 0;
    };

    double NormalizeAngle(double angle)
    {
        while (angle > 180.0) angle -= 360.0;
        while (angle < -180.0) angle += 360.0;
        return angle;
    }

    FRotator LookAt(const FVector& from, const FVector& to)
    {
        const FVector d = to - from;
        const double horizontal = std::sqrt(d.X * d.X + d.Y * d.Y);
        FRotator out{};
        out.Pitch = std::atan2(d.Z, horizontal) * 180.0 / Pi;
        out.Yaw = std::atan2(d.Y, d.X) * 180.0 / Pi;
        out.Roll = 0.0;
        return out;
    }

    bool ApplyMouseAim(const Vector2& target, const Vector2& center, float smoothing, float strength)
    {
        const double smooth = std::max(1.0, static_cast<double>(smoothing));
        const double factor = static_cast<double>(strength) / smooth;
        double dx = static_cast<double>(target.x - center.x) * factor;
        double dy = static_cast<double>(target.y - center.y) * factor;

        dx = std::clamp(dx, -350.0, 350.0);
        dy = std::clamp(dy, -350.0, 350.0);

        LONG moveX = static_cast<LONG>(std::lround(dx));
        LONG moveY = static_cast<LONG>(std::lround(dy));
        if (moveX == 0 && std::abs(target.x - center.x) > 0.75f)
            moveX = target.x > center.x ? 1 : -1;
        if (moveY == 0 && std::abs(target.y - center.y) > 0.75f)
            moveY = target.y > center.y ? 1 : -1;
        if (moveX == 0 && moveY == 0)
            return false;

        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = moveX;
        input.mi.dy = moveY;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        return SendInput(1, &input, sizeof(input)) == 1;
    }

    bool GetCapsuleTarget(uintptr_t actor, const std::string& targetName, FVector& out)
    {
        FVector location{};
        if (!GameAccess::GetActorLocation(actor, location))
            return false;
        const double halfHeight = static_cast<double>(
            GameAccess::GetCapsuleHalfHeight(actor));
        double heightFactor = 0.30;
        if (targetName == "head") heightFactor = 0.78;
        else if (targetName == "neck") heightFactor = 0.58;
        else if (targetName == "chest") heightFactor = 0.32;
        else if (targetName == "stomach" || targetName == "pelvis") heightFactor = -0.10;
        else if (targetName == "arm_l" || targetName == "arm_r" ||
                 targetName == "hand_l" || targetName == "hand_r") heightFactor = 0.18;
        else if (targetName == "leg_l" || targetName == "leg_r") heightFactor = -0.48;
        else if (targetName == "foot_l" || targetName == "foot_r") heightFactor = -0.78;
        out = location + FVector(0.0, 0.0, halfHeight * heightFactor);
        return out.IsFinite();
    }

    bool QueueVisibilitySamples(const std::vector<AimCandidate>& candidates,
                                const FVector& viewPoint)
    {
        if (candidates.empty() || !viewPoint.IsFinite() || !GetGameWindowThreadId())
            return false;
        const uintptr_t world = GameAccess::GetWorld();
        {
            std::lock_guard<std::mutex> lock(g_visibilityMutex);
            if (g_visibilityWorld != world)
            {
                g_visibilityCache.clear();
                g_visibilityWorld = world;
            }
        }

        const ULONGLONG now = GetTickCount64();
        if (g_visibilityTaskPending.load() ||
            (g_lastVisibilityRequestAt && now - g_lastVisibilityRequestAt < 45))
            return false;

        std::vector<uintptr_t> actors;
        actors.reserve(std::min<size_t>(candidates.size(), 8));
        for (const AimCandidate& candidate : candidates)
        {
            if (!candidate.Actor ||
                std::find(actors.begin(), actors.end(), candidate.Actor) != actors.end())
                continue;
            actors.push_back(candidate.Actor);
            if (actors.size() >= 8)
                break;
        }
        if (actors.empty())
            return false;

        const int queuedCount = static_cast<int>(actors.size());
        g_lastVisibilityRequestAt = now;
        g_visibilityTaskPending.store(true);
        if (!QueueGameThreadTask([actors = std::move(actors), viewPoint, world]()
            {
                std::vector<std::pair<uintptr_t, VisibilitySample>> samples;
                samples.reserve(actors.size());
                for (const uintptr_t actor : actors)
                {
                    if (!GameAccess::IsIRRCharacter(actor))
                        continue;
                    bool visible = false;
                    if (GameAccess::HasLineOfSight(actor, viewPoint, visible))
                        samples.push_back({ actor, { visible, GetTickCount64() } });
                }
                const ULONGLONG now = GetTickCount64();
                {
                    std::lock_guard<std::mutex> lock(g_visibilityMutex);
                    if (g_visibilityWorld == world)
                    {
                        for (const auto& sample : samples)
                            g_visibilityCache[sample.first] = sample.second;
                        for (auto it = g_visibilityCache.begin();
                             it != g_visibilityCache.end();)
                        {
                            if (!it->second.SampledAt || now - it->second.SampledAt > 1000)
                                it = g_visibilityCache.erase(it);
                            else
                                ++it;
                        }
                    }
                }
                g_visibilitySampleThreadId.store(GetCurrentThreadId());
                g_visibilityTaskPending.store(false);
            }, false))
        {
            g_visibilityTaskPending.store(false);
            return false;
        }
        g_aimDiagnostics.VisibilityQueriesQueued = queuedCount;
        return true;
    }

    bool GetCachedVisibility(uintptr_t actor, bool& visible)
    {
        visible = false;
        std::lock_guard<std::mutex> lock(g_visibilityMutex);
        const auto found = g_visibilityCache.find(actor);
        if (found == g_visibilityCache.end() || !found->second.SampledAt ||
            GetTickCount64() - found->second.SampledAt > 250)
            return false;
        visible = found->second.Visible;
        return true;
    }
}

namespace Aimbot
{
    bool bEnabled = false;
    bool bDrawFov = true;
    bool bSmoothAim = true;
    bool bUseMouseInput = true;
    bool bVisibilityCheck = true;
    bool bPrediction = true;
    float smoothAmount = 6.0f;
    float aimStrength = 1.0f;
    float fovRadius = 180.0f;
    float maxDistanceMeters = 150.0f;
    float predictionMultiplier = 1.0f;
    float maxPredictionTime = 2.0f;
    std::string selectedBone = "head";

    static const std::vector<std::string> bones = {
        "head", "neck", "chest", "stomach", "arm_l", "arm_r",
        "leg_l", "leg_r", "foot_l", "foot_r"
    };

    void Enable() { bEnabled = true; }
    void Disable() { bEnabled = false; }

    void Run()
    {
        FRotator observed{};
        bool rotationChanged = false;
        if (g_aimDiagnostics.AimAttempted && g_aimDiagnostics.Controller &&
            Memory::TryRead(g_aimDiagnostics.Controller + Offsets::AController_ControlRotation,
                            observed) && observed.IsFinite())
        {
            rotationChanged = std::abs(observed.Pitch - g_aimDiagnostics.RotationBefore.Pitch) > 0.001 ||
                              std::abs(observed.Yaw - g_aimDiagnostics.RotationBefore.Yaw) > 0.001;
        }
        g_aimDiagnostics = {};
        g_aimDiagnostics.RotationObserved = observed;
        g_aimDiagnostics.RotationChangedAfterAttempt = rotationChanged;
        g_aimDiagnostics.VisibilityTaskPending = g_visibilityTaskPending.load();
        g_aimDiagnostics.VisibilitySampleThreadId = g_visibilitySampleThreadId.load();

        if (!ImGui::GetCurrentContext())
            return;

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const Vector2 center{ display.x * 0.5f, display.y * 0.5f };

        if (bDrawFov && fovRadius > 1.0f)
            Renderer::DrawCircle(center.x, center.y, fovRadius, IM_COL32(255, 255, 255, 100));

        // Keep the visual FOV available for tuning, but never submit aim input while
        // the menu owns the mouse. GetAsyncKeyState still sees physical RMB even when
        // Win32 raw input is swallowed, so this guard prevents menu clicks from aiming.
        if (Menu::bOpen)
        {
            g_lockedActor = 0;
            g_rmbWasHeld = false;
            return;
        }

        if (!bEnabled)
        {
            g_lockedActor = 0;
            g_rmbWasHeld = false;
            return;
        }

        const uintptr_t controller = GameAccess::GetLocalController();
        const uintptr_t localPawn = GameAccess::GetLocalPawn();
        const auto camera = GameAccess::GetCamera();
        if (!controller || !localPawn || !camera.Valid)
            return;
        g_aimDiagnostics.Controller = controller;
        g_aimDiagnostics.VisibilityRequired = bVisibilityCheck;
        g_aimDiagnostics.RmbHeld = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (g_aimDiagnostics.RmbHeld && !g_rmbWasHeld)
        {
            std::lock_guard<std::mutex> lock(g_visibilityMutex);
            g_visibilityCache.clear();
            g_lastVisibilityRequestAt = 0;
        }
        if (!g_aimDiagnostics.RmbHeld)
            g_lockedActor = 0;
        g_rmbWasHeld = g_aimDiagnostics.RmbHeld;

        FVector localPos{};
        if (!GameAccess::GetActorLocation(localPawn, localPos))
            return;

        std::vector<AimCandidate> candidates;
        candidates.reserve(GameAccess::GetCharacters().size());

        for (const uintptr_t actor : GameAccess::GetCharacters())
        {
            ++g_aimDiagnostics.CharactersScanned;
            if (!GameAccess::IsEnemyCharacter(actor))
                continue;
            ++g_aimDiagnostics.EnemyCandidates;
            if (!GameAccess::IsLivingCharacter(actor))
                continue;
            ++g_aimDiagnostics.LivingCandidates;

            FVector actorPos{};
            if (!GameAccess::GetActorLocation(actor, actorPos))
                continue;
            if (localPos.Distance(actorPos) / 100.0 > maxDistanceMeters)
                continue;
            ++g_aimDiagnostics.DistanceCandidates;

            FVector target{};
            bool usedCapsuleFallback = false;
            // Prefer the exact cached, runtime-validated position used by Bone ESP. The
            // current build reports an empty bone cache, so a capsule-relative position
            // keeps target acquisition operational while skeletal validation is repaired.
            if (GameAccess::GetVerifiedBoneTarget(actor, selectedBone, target))
                ++g_aimDiagnostics.VerifiedBoneTargets;
            else
            {
                if (!GetCapsuleTarget(actor, selectedBone, target))
                    continue;
                usedCapsuleFallback = true;
                ++g_aimDiagnostics.CapsuleFallbackTargets;
            }

            // Selection uses one coherent current-camera snapshot. Calling the engine
            // projection function once per actor from Present sampled a different game
            // frame and was both jittery and expensive.
            Vector2 screen{};
            if (!WorldToScreen::Convert(target, screen, camera.Location, camera.Rotation,
                                        camera.FOV, static_cast<int>(display.x),
                                        static_cast<int>(display.y)))
                continue;
            ++g_aimDiagnostics.ProjectedTargets;

            const float dx = screen.x - center.x;
            const float dy = screen.y - center.y;
            const float screenDistance = std::sqrt(dx * dx + dy * dy);
            if (screenDistance <= fovRadius)
                ++g_aimDiagnostics.InFovTargets;
            // A crouched head can be materially below its coarse capsule estimate.
            // Admit a bounded margin while RMB is held, then enforce the real FOV
            // after the live body-part point has been queried below.
            const float coarseRadius = g_aimDiagnostics.RmbHeld ?
                fovRadius + 120.0f : fovRadius;
            if (screenDistance <= coarseRadius)
                candidates.push_back({ actor, target, screen, screenDistance,
                                       usedCapsuleFallback, false, 0 });
        }

        if (candidates.empty())
        {
            g_lockedActor = 0;
            return;
        }
        std::sort(candidates.begin(), candidates.end(),
            [](const AimCandidate& left, const AimCandidate& right)
            { return left.ScreenDistance < right.ScreenDistance; });

        // Refine only the closest crosshair shortlist through the game's own body
        // component. This follows the current animation pose (crouch/prone/lean)
        // while bounding ProcessEvent work so large AI groups do not reduce FPS.
        if (g_aimDiagnostics.RmbHeld)
        {
            constexpr size_t MaxPoseQueries = 8;
            if (candidates.size() > MaxPoseQueries)
            {
                // Do not evict the current sticky target merely because its coarse
                // standing-height estimate ranked below the pose-query shortlist.
                if (g_lockedActor)
                {
                    const auto locked = std::find_if(candidates.begin() + MaxPoseQueries,
                        candidates.end(), [](const AimCandidate& candidate)
                        { return candidate.Actor == g_lockedActor; });
                    if (locked != candidates.end())
                        candidates[MaxPoseQueries - 1] = *locked;
                }
                candidates.resize(MaxPoseQueries);
            }
            std::vector<uintptr_t> poseActors;
            poseActors.reserve(candidates.size());
            for (const AimCandidate& candidate : candidates)
                poseActors.push_back(candidate.Actor);
            GameAccess::RequestPoseSamples(poseActors, 65);
            for (AimCandidate& candidate : candidates)
            {
                FVector poseTarget{};
                uintptr_t bodyComponent = 0;
                if (!GameAccess::GetPoseAwareBodyTarget(candidate.Actor,
                        selectedBone, poseTarget, &bodyComponent))
                {
                    ++g_aimDiagnostics.PoseAwareFailures;
                    continue;
                }
                Vector2 poseScreen{};
                if (!WorldToScreen::Convert(poseTarget, poseScreen,
                        camera.Location, camera.Rotation, camera.FOV,
                        static_cast<int>(display.x), static_cast<int>(display.y)))
                {
                    ++g_aimDiagnostics.PoseAwareFailures;
                    candidate.ScreenDistance = std::numeric_limits<float>::infinity();
                    continue;
                }
                const float dx = poseScreen.x - center.x;
                const float dy = poseScreen.y - center.y;
                candidate.Target = poseTarget;
                candidate.Screen = poseScreen;
                candidate.ScreenDistance = std::sqrt(dx * dx + dy * dy);
                candidate.CapsuleFallback = false;
                candidate.PoseAware = true;
                candidate.BodyComponent = bodyComponent;
                ++g_aimDiagnostics.PoseAwareTargets;
            }
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                [&](const AimCandidate& candidate)
                { return candidate.ScreenDistance > fovRadius; }), candidates.end());
            std::sort(candidates.begin(), candidates.end(),
                [](const AimCandidate& left, const AimCandidate& right)
                { return left.ScreenDistance < right.ScreenDistance; });
            if (candidates.empty())
            {
                g_lockedActor = 0;
                return;
            }
        }

        if (bVisibilityCheck && g_aimDiagnostics.RmbHeld)
            QueueVisibilitySamples(candidates, camera.Location);

        auto visible = [&](const AimCandidate& candidate) -> bool
        {
            if (!bVisibilityCheck || !g_aimDiagnostics.RmbHeld)
                return true;
            bool lineOfSight = false;
            if (!GetCachedVisibility(candidate.Actor, lineOfSight))
            {
                ++g_aimDiagnostics.VisibilityCacheMisses;
                return false;
            }
            ++g_aimDiagnostics.VisibilityCacheHits;
            ++g_aimDiagnostics.LineOfSightChecks;
            if (!lineOfSight)
            {
                ++g_aimDiagnostics.OccludedTargets;
                return false;
            }
            return true;
        };

        const AimCandidate* best = nullptr;
        const AimCandidate* locked = nullptr;
        if (g_lockedActor && g_aimDiagnostics.RmbHeld)
        {
            const auto foundLock = std::find_if(candidates.begin(), candidates.end(),
                [](const AimCandidate& candidate)
                { return candidate.Actor == g_lockedActor; });
            if (foundLock != candidates.end() && visible(*foundLock))
                locked = &*foundLock;
        }

        for (const AimCandidate& candidate : candidates)
        {
            if (locked && candidate.Actor == locked->Actor)
                continue;
            if (visible(candidate))
            {
                best = &candidate;
                break;
            }
        }
        if (locked)
        {
            // Retain a live target while aiming to prevent frame-to-frame target
            // oscillation. A new enemy wins only when the crosshair is decisively
            // closer to it, preserving explicit user target priority.
            if (!best || !(best->ScreenDistance + 15.0f <
                           locked->ScreenDistance * 0.55f))
            {
                best = locked;
                g_aimDiagnostics.StickyTarget = true;
            }
        }
        if (!best)
        {
            g_lockedActor = 0;
            return;
        }

        AimCandidate chosen = *best;
        if (g_aimDiagnostics.RmbHeld)
            g_lockedActor = chosen.Actor;

        // When the verified skeleton cache is unavailable, the selected head target
        // is refined once through the actor's real eye viewpoint. This avoids aiming
        // at an approximate capsule percentage without adding a call for every actor.
        if (!chosen.PoseAware && chosen.CapsuleFallback && selectedBone == "head" &&
            g_aimDiagnostics.RmbHeld)
        {
            FVector eyes{};
            Vector2 eyesScreen{};
            if (GameAccess::GetActorEyesViewPoint(chosen.Actor, eyes) &&
                WorldToScreen::Convert(eyes, eyesScreen, camera.Location, camera.Rotation,
                                       camera.FOV, static_cast<int>(display.x),
                                       static_cast<int>(display.y)))
            {
                chosen.Target = eyes;
                chosen.Screen = eyesScreen;
                g_aimDiagnostics.UsedEyeViewPoint = true;
            }
        }

        FVector targetVelocity{};
        GameAccess::GetActorVelocity(chosen.Actor, targetVelocity);
        g_aimDiagnostics.TargetVelocity = targetVelocity;
        g_aimDiagnostics.ProjectileSpeed = GameAccess::GetProjectileSpeedCmPerSecond();
        if (bPrediction && g_aimDiagnostics.RmbHeld)
        {
            FVector predicted{};
            float flightTime = 0.0f;
            bool usedGameSolver = false;
            if (GameAccess::PredictBallisticAim(camera.Location, chosen.Target,
                    targetVelocity * static_cast<double>(predictionMultiplier),
                    maxPredictionTime, predicted, flightTime, usedGameSolver))
            {
                chosen.Target = predicted;
                Vector2 predictedScreen{};
                if (WorldToScreen::Convert(chosen.Target, predictedScreen,
                        camera.Location, camera.Rotation, camera.FOV,
                        static_cast<int>(display.x), static_cast<int>(display.y)))
                    chosen.Screen = predictedScreen;
                g_aimDiagnostics.PredictionApplied = true;
                g_aimDiagnostics.UsedGameBallisticSolver = usedGameSolver;
                g_aimDiagnostics.PredictedFlightTime = flightTime;
            }
        }

        g_aimDiagnostics.TargetFound = true;
        g_aimDiagnostics.TargetActor = chosen.Actor;
        g_aimDiagnostics.TargetWorld = chosen.Target;
        g_aimDiagnostics.UsedCapsuleFallback = chosen.CapsuleFallback;
        g_aimDiagnostics.UsedPoseAwareBodyPart = chosen.PoseAware;
        g_aimDiagnostics.TargetBodyComponent = chosen.BodyComponent;
        g_aimDiagnostics.TargetVisible = !bVisibilityCheck ||
            !g_aimDiagnostics.RmbHeld || best != nullptr;

        // Gold marker confirms that a target was acquired and shows the exact aim point.
        Renderer::DrawCircle(chosen.Screen.x, chosen.Screen.y, 7.0f,
                             IM_COL32(255, 215, 0, 230));

        // Hold RMB to aim (normally the game's ADS button).
        if (!g_aimDiagnostics.RmbHeld)
            return;

        Memory::TryRead(controller + Offsets::AController_ControlRotation,
                        g_aimDiagnostics.RotationBefore);
        g_aimDiagnostics.AimAttempted = true;

        if (bUseMouseInput)
        {
            // The old direct ControlRotation write was immediately overwritten by the game's
            // Enhanced Input/camera update. Feed the game real relative mouse-look input instead.
            g_aimDiagnostics.UsedMouseInput = true;
            g_aimDiagnostics.DirectWriteSucceeded = ApplyMouseAim(
                chosen.Screen, center, bSmoothAim ? smoothAmount : 1.0f,
                aimStrength);
            return;
        }

        // Direct UE rotation mode now uses the dump-confirmed SetControlRotation
        // UFunction; a raw field write remains only as an emergency fallback.
        FRotator current = Memory::Read<FRotator>(controller + Offsets::AController_ControlRotation);
        const FRotator desired = LookAt(camera.Location, chosen.Target);
        const double factor = bSmoothAim ? std::clamp(1.0 / static_cast<double>(smoothAmount), 0.01, 1.0) : 1.0;
        current.Pitch += NormalizeAngle(desired.Pitch - current.Pitch) * factor;
        current.Yaw += NormalizeAngle(desired.Yaw - current.Yaw) * factor;
        current.Pitch = std::clamp(current.Pitch, -89.0, 89.0);
        current.Yaw = NormalizeAngle(current.Yaw);
        current.Roll = 0.0;
        g_aimDiagnostics.UsedSetControlRotationFunction = true;
        g_aimDiagnostics.DirectWriteSucceeded = GameAccess::InvokeFunctionRaw(
            controller, FunctionIndices::Controller_SetControlRotation,
            &current, sizeof(current));
        if (!g_aimDiagnostics.DirectWriteSucceeded)
            g_aimDiagnostics.DirectWriteSucceeded = Memory::Write(
                controller + Offsets::AController_ControlRotation, current);
    }

    void RenderTab()
    {
        ImGui::Checkbox("Enable Aimbot", &bEnabled);
        ImGui::Checkbox("Draw FOV", &bDrawFov);
        ImGui::SliderFloat("FOV Radius", &fovRadius, 25.0f, 600.0f, "%.0f px");
        ImGui::SliderFloat("Max Distance", &maxDistanceMeters, 10.0f, 500.0f, "%.0f m");
        ImGui::Checkbox("Smooth Aim", &bSmoothAim);
        if (bSmoothAim)
            ImGui::SliderFloat("Smooth Amount", &smoothAmount, 1.0f, 25.0f, "%.1f");

        ImGui::Checkbox("Use Mouse Input Aim", &bUseMouseInput);
        if (bUseMouseInput)
            ImGui::SliderFloat("Aim Strength", &aimStrength, 0.10f, 2.50f, "%.2f");

        ImGui::Checkbox("Wall / visibility check", &bVisibilityCheck);
        ImGui::Checkbox("Projectile movement prediction", &bPrediction);
        if (bPrediction)
        {
            ImGui::SliderFloat("Lead multiplier", &predictionMultiplier,
                               0.25f, 2.0f, "%.2fx");
            ImGui::SliderFloat("Maximum flight time", &maxPredictionTime,
                               0.1f, 5.0f, "%.2f s");
        }

        ImGui::Text("Target bone:");
        for (const auto& bone : bones)
        {
            if (ImGui::RadioButton(bone.c_str(), selectedBone == bone))
                selectedBone = bone;
        }
        ImGui::TextWrapped("Hold Right Mouse Button to aim. The closest visible enemy to the crosshair is selected, then held with target hysteresis so adjacent actors cannot make the aim jump. The game's alternate LineOfSightTo checks reject walls while accepting exposed head/body points when the actor origin is behind low cover.");
        ImGui::TextWrapped("With smoothing enabled, Mouse Input Aim follows normal game input. With smoothing disabled, the exact dump-validated SetControlRotation path is used automatically so a sensitivity-dependent mouse delta cannot overshoot the target.");
        ImGui::TextWrapped("Targets must be present in the validated local IRRTeamComponent Hostiles array; unknown/neutral IRR candidates are never selected.");
        ImGui::Text("Scan %d | hostile %d | living %d | range %d | projected %d | FOV %d",
            g_aimDiagnostics.CharactersScanned, g_aimDiagnostics.EnemyCandidates,
            g_aimDiagnostics.LivingCandidates, g_aimDiagnostics.DistanceCandidates,
            g_aimDiagnostics.ProjectedTargets, g_aimDiagnostics.InFovTargets);
        ImGui::Text("Aim source: %s | body points %d | body failures %d",
            g_aimDiagnostics.TargetFound ?
                (g_aimDiagnostics.UsedPoseAwareBodyPart ? "POSE-AWARE BODY PART" :
                 (g_aimDiagnostics.UsedCapsuleFallback ? "CAPSULE FALLBACK" :
                  "VERIFIED BONE")) : "NONE",
            g_aimDiagnostics.PoseAwareTargets,
            g_aimDiagnostics.PoseAwareFailures);
        ImGui::Text("LOS checks %d | occluded %d | selected visible %s | sticky %s",
            g_aimDiagnostics.LineOfSightChecks, g_aimDiagnostics.OccludedTargets,
            g_aimDiagnostics.TargetVisible ? "YES" : "NO",
            g_aimDiagnostics.StickyTarget ? "YES" : "NO");
        ImGui::Text("Prediction %s | solver %s | flight %.3f s | projectile %.1f m/s",
            g_aimDiagnostics.PredictionApplied ? "ON" : "WAIT",
            g_aimDiagnostics.UsedGameBallisticSolver ? "EasyBallistics" : "analytic fallback",
            g_aimDiagnostics.PredictedFlightTime,
            g_aimDiagnostics.ProjectileSpeed / 100.0f);
    }

    void Render() { RenderTab(); }
    const Diagnostics& GetDiagnostics() { return g_aimDiagnostics; }
}
