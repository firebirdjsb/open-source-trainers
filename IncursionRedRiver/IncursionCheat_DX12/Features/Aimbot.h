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
        bool UsedControllerLookInput = false;
        bool UsedCapsuleFallback = false;
        bool UsedSetControlRotationFunction = false;
        bool DirectWriteSucceeded = false;
        bool RotationChangedAfterAttempt = false;
        bool StickyTarget = false;
        bool UsedEyeViewPoint = false;
        bool UsedPoseAwareBodyPart = false;
        bool PredictionApplied = false;
        bool UsedGameBallisticSolver = false;
        bool UsedExposedPoint = false;
        float PredictedFlightTime = 0.0f;
        float ProjectileSpeed = 0.0f;
        uintptr_t TargetBodyComponent = 0;
        int CharactersScanned = 0;
        int EnemyCandidates = 0;
        int LivingCandidates = 0;
        int DistanceCandidates = 0;
        int LiveBodyTargets = 0;
        int CapsuleFallbackTargets = 0;
        int PoseAwareTargets = 0;
        int PoseAwareFailures = 0;
        int ProjectedTargets = 0;
        int InFovTargets = 0;
        int VisibilityKnownTargets = 0;
        int VisibilityVisibleTargets = 0;
        int VisibilityHiddenTargets = 0;
        int VisibilityUnknownTargets = 0;
    };

    extern bool bEnabled;
    extern bool bDrawFov;
    extern bool bSmoothAim;
    extern bool bUseMouseInput;
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
