#pragma once
#include "../sdk/Structs.h"
#include <cstdint>
#include <string>

namespace Aimbot
{
    struct Diagnostics
    {
        uintptr_t Controller = 0;
        uintptr_t TargetActor = 0;
        FVector TargetWorld{};
        FVector TargetVelocity{};
        FRotator RotationBefore{};
        FRotator RotationObserved{};
        bool TargetFound = false;
        bool RmbHeld = false;
        bool LmbHeld = false;
        bool ActivationHeld = false;
        bool AimAttempted = false;
        bool UsedMouseInput = false;
        bool UsedCapsuleFallback = false;
        bool UsedSetControlRotationFunction = false;
        bool DirectWriteSucceeded = false;
        bool RotationChangedAfterAttempt = false;
        bool VisibilityRequired = false;
        bool TargetVisible = false;
        bool StickyTarget = false;
        bool UsedEyeViewPoint = false;
        bool UsedPoseAwareBodyPart = false;
        bool PredictionApplied = false;
        bool UsedGameBallisticSolver = false;
        float PredictedFlightTime = 0.0f;
        float ProjectileSpeed = 0.0f;
        uintptr_t TargetBodyComponent = 0;
        int CharactersScanned = 0;
        int EnemyCandidates = 0;
        int LivingCandidates = 0;
        int DistanceCandidates = 0;
        int VerifiedBoneTargets = 0;
        int CapsuleFallbackTargets = 0;
        int PoseAwareTargets = 0;
        int PoseAwareFailures = 0;
        int ProjectedTargets = 0;
        int InFovTargets = 0;
        int LineOfSightChecks = 0;
        int OccludedTargets = 0;
        int VisibilityCacheHits = 0;
        int VisibilityCacheMisses = 0;
        int VisibilityQueriesQueued = 0;
        int TargetSpherePasses = 0;
        uint32_t VisibilitySampleThreadId = 0;
        bool VisibilityTaskPending = false;
    };

    extern bool bEnabled;
    extern bool bDrawFov;
    extern bool bSmoothAim;
    extern bool bUseMouseInput;
    extern bool bVisibilityCheck;
    extern bool bAimOnFire;
    extern bool bPrediction;
    extern float smoothAmount;
    extern float aimStrength;
    extern float fovRadius;
    extern float maxDistanceMeters;
    extern float predictionMultiplier;
    extern float maxPredictionTime;
    extern std::string selectedBone;

    void Enable();
    void Disable();
    void Run();
    const Diagnostics& GetDiagnostics();
    void RenderTab();
    void Render();
}
