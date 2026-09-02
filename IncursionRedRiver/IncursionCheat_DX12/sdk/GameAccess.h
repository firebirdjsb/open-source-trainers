#pragma once

#include "Structs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace GameAccess
{
    enum class Source
    {
        None,
        GWorld,
        Viewport,
        GameEngine,
        World,
        LocalPlayers,
        ControllerPlayer,
        ObjectArray
    };

    struct Camera
    {
        FVector Location{};
        FRotator Rotation{};
        float FOV = 90.0f;
        bool Valid = false;
    };

    struct Health
    {
        uintptr_t Component = 0;
        uintptr_t Attribute = 0;
        float Current = 0.0f;
        float Minimum = 0.0f;
        float Maximum = 0.0f;
        bool Enabled = false;
        bool Dead = false;
        bool Valid = false;
    };

    struct BonePoint
    {
        int32_t Index = -1;
        int32_t ParentIndex = -1;
        FVector Component{};
        FVector World{};
    };

    struct BoneDiagnostics
    {
        uintptr_t Actor = 0;
        uintptr_t Mesh = 0;
        uintptr_t SkinnedAsset = 0;
        uintptr_t Skeleton = 0;
        uintptr_t TransformData = 0;
        int32_t TransformCount = 0;
        int32_t TransformCapacity = 0;
        uintptr_t ParentArray = 0;
        int32_t ParentCount = 0;
        int32_t ParentInfoStride = 0;
        int32_t ParentFieldOffset = 0;
        std::array<FVector, 4> SampleTranslations{};
        int32_t SampleCount = 0;
        bool MeshTypeValid = false;
        bool TransformArrayValid = false;
        bool ParentIndicesValid = false;
    };

    struct PoseCacheDiagnostics
    {
        int32_t CachedActors = 0;
        int32_t LastRequestedActors = 0;
        int32_t LastSampledActors = 0;
        int32_t LastAggregateActors = 0;
        int32_t LastFallbackActors = 0;
        uint64_t LastCompletedAt = 0;
        uint32_t LastSampleThreadId = 0;
        bool TaskPending = false;
    };

    struct RuntimeDiagnostics
    {
        uint64_t Serial = 0;
        uintptr_t ModuleBase = 0;
        uintptr_t GWorldSlot = 0;
        uintptr_t RawGWorld = 0;
        bool RawGWorldPlausible = false;
        uintptr_t World = 0;
        Source WorldSource = Source::None;
        bool WorldIsActiveRaid = false;
        uintptr_t PersistentLevel = 0;
        uintptr_t LevelOwningWorld = 0;
        uintptr_t ActorArrayData = 0;
        int32_t ActorCount = 0;
        int32_t ActorCapacity = 0;

        uintptr_t GObjectsAddress = 0;
        uintptr_t ResolvedObjectArray = 0;
        uintptr_t ObjectChunkTable = 0;
        int32_t ObjectCount = 0;
        int32_t ObjectCapacity = 0;
        int32_t ObjectItemStride = 0;
        bool ObjectArrayValid = false;
        bool ObjectArrayUsedSectionScan = false;
        int32_t ObjectArrayProbeScore = 0;
        int32_t ObjectWorldCount = 0;
        int32_t ObjectGameEngineCount = 0;
        int32_t ObjectViewportCount = 0;
        int32_t ObjectLocalPlayerCount = 0;
        int32_t ObjectControllerCount = 0;
        int32_t ObjectGameInstanceCount = 0;
        int32_t ObjectCharacterCount = 0;
        int32_t ObjectWeaponCount = 0;

        uintptr_t GameEngine = 0;
        uintptr_t EngineGameInstance = 0;
        uintptr_t ViewportClient = 0;
        uintptr_t ViewportWorld = 0;
        uintptr_t ViewportGameInstance = 0;

        uintptr_t WorldGameInstance = 0;
        uintptr_t GameInstance = 0;
        Source GameInstanceSource = Source::None;
        uintptr_t LocalPlayersData = 0;
        int32_t LocalPlayersCount = 0;
        int32_t LocalPlayersCapacity = 0;
        uintptr_t LocalPlayer = 0;
        Source LocalPlayerSource = Source::None;
        uintptr_t PlayerController = 0;
        Source ControllerSource = Source::None;
        uintptr_t ControllerPlayer = 0;
        uintptr_t ControllerPawn = 0;
        uintptr_t AcknowledgedPawn = 0;
        uintptr_t Pawn = 0;
        uintptr_t CameraManager = 0;

        uintptr_t LocalTeamComponent = 0;
        uintptr_t HostileArrayData = 0;
        int32_t HostileArrayCount = 0;
        int32_t HostileArrayCapacity = 0;
        int32_t HostileCharacterCount = 0;
        bool LocalTeamComponentTypeValid = false;
        bool HostileArrayValid = false;

        uintptr_t EquippedWeapon = 0;
        uintptr_t WeaponComponent = 0;
        uintptr_t BallisticBarrel = 0;
        float ProjectileSpeedCmPerSecond = 0.0f;
        uintptr_t InventoryComponent = 0;
        uintptr_t SenseStimulusComponent = 0;
        bool SenseStimulusComponentTypeValid = false;
        uintptr_t StaminaObject = 0;
        uintptr_t StaminaAttribute = 0;
        int32_t StaminaAttributeCount = 0;
        uintptr_t StaminaArmObject = 0;
        uintptr_t WeaponRecoilObject = 0;
        bool GameInstanceTypeValid = false;
        bool PawnTypeValid = false;
        bool WeaponTypeValid = false;
        bool WeaponComponentTypeValid = false;

        uintptr_t ProcessEventAddress = 0;
        uintptr_t LastFunctionObject = 0;
        int32_t LastFunctionIndex = -1;
        bool ProcessEventValid = false;
        bool LastProcessEventCallSucceeded = false;

        int32_t WorldCandidateCount = 0;
        int32_t ScannedControllerCount = 0;
        int32_t ScannedCharacterCount = 0;
        int32_t ActiveCharacterCount = 0;
        int32_t ValidatedBoneActorCount = 0;
        int32_t ValidatedBonePointCount = 0;
        const char* FailureStage = "not refreshed";
    };

    void Refresh();
    void Reset();

    const RuntimeDiagnostics& GetDiagnostics();
    const BoneDiagnostics& GetBoneDiagnostics();
    PoseCacheDiagnostics GetPoseCacheDiagnostics();
    const char* SourceName(Source source);

    uintptr_t GetWorld();
    uintptr_t GetPersistentLevel();
    uintptr_t GetGameInstance();
    uintptr_t GetLocalPlayer();
    uintptr_t GetLocalController();
    uintptr_t GetLocalPawn();
    uintptr_t GetCameraManager();
    uintptr_t GetEquippedWeapon();
    uintptr_t GetWeaponComponent();
    uintptr_t GetBallisticBarrel();
    uintptr_t GetInventoryComponent();
    uintptr_t GetStaminaObject();
    uintptr_t GetStaminaAttribute();
    uintptr_t GetStaminaArmObject();
    uintptr_t GetWeaponRecoilObject();
    const std::vector<uintptr_t>& GetStaminaAttributes();

    Camera GetCamera();
    Camera GetRenderCamera();
    bool InvokeFunctionRaw(uintptr_t object, int32_t functionIndex,
                           void* params, size_t paramSize);
    bool InvokeBooleanFunction(uintptr_t object, int32_t functionIndex, bool value);
    bool QueryBooleanFunction(uintptr_t object, int32_t functionIndex,
                              bool& outValue);
    bool QueryByteFunction(uintptr_t object, int32_t functionIndex,
                           uint8_t& outValue);
    bool QueryFloatFunction(uintptr_t object, int32_t functionIndex,
                            float& outValue);
    bool GetObjectNameToken(int32_t objectIndex, FName& outName);
    // Read-only helper for dump-verified object indices. Returns 0 until the live
    // GUObjectArray has passed the existing structural validation.
    uintptr_t GetObjectByIndex(int32_t objectIndex);
    bool HasLineOfSight(uintptr_t actor, const FVector& targetPoint,
                        bool& outVisible, bool* outUsedTargetSphere = nullptr);
    bool GetActorEyesViewPoint(uintptr_t actor, FVector& outLocation);
    // Uses IRRBaseCharacter::BodyComponent so the returned point follows the live
    // animation pose (including crouch/prone) instead of a standing capsule ratio.
    bool GetPoseAwareBodyTarget(uintptr_t actor, const std::string& targetName,
                                FVector& outLocation,
                                uintptr_t* outBodyComponent = nullptr);
    // Budgeted living AI frequently leave the base component-space transform array
    // empty. Sample the game's authoritative IRRBodyComponent pose on the window/game
    // thread, then serve immutable cached points to Present/ESP/aim acquisition.
    bool RequestPoseSamples(const std::vector<uintptr_t>& actors,
                            uint32_t minimumIntervalMs = 75);
    std::vector<BonePoint> GetCachedPoseSkeleton(uintptr_t actor,
                                                 uint32_t maximumAgeMs = 500);
    bool GetActorVelocity(uintptr_t actor, FVector& outVelocity);
    bool PredictBallisticAim(const FVector& start, const FVector& target,
                             const FVector& targetVelocity, float maxTime,
                             FVector& outAimPoint, float& outFlightTime,
                             bool& outUsedGameSolver);
    float GetProjectileSpeedCmPerSecond();
    bool ProjectWorldToScreen(const FVector& world, Vector2& out);
    bool GetActorLocation(uintptr_t actor, FVector& out);
    float GetCapsuleHalfHeight(uintptr_t actor, float fallback = 90.0f);
    float GetCapsuleRadius(uintptr_t actor, float fallback = 40.0f);
    bool IsInstanceOf(uintptr_t object, uintptr_t targetClass);
    bool IsIRRCharacter(uintptr_t actor);
    bool IsEnemyCharacter(uintptr_t actor);

    std::vector<BonePoint> GetBonePoints(uintptr_t actor, int32_t maxBones = 256);
    bool GetVerifiedBoneTarget(uintptr_t actor, const std::string& targetName, FVector& out);
    Health GetHealth(uintptr_t actor);
    bool IsLivingCharacter(uintptr_t actor);
    std::wstring GetPlayerName(uintptr_t actor);

    const std::vector<uintptr_t>& GetActors();
    const std::vector<uintptr_t>& GetCharacters();
}
