#include "GameAccess.h"

#include "Offsets.h"
#include "../Memory/Memory.h"
#include "../hooks/PresentHook.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
    struct ObjectArrayLayout
    {
        uintptr_t Root = 0;
        uintptr_t Chunks = 0;
        int32_t Count = 0;
        int32_t Capacity = 0;
        int32_t NumChunks = 0;
        int32_t ItemStride = 0;
        bool Chunked = true;
        bool Valid = false;
    };

    struct ClassPointers
    {
        uintptr_t Function = 0;
        uintptr_t Level = 0;
        uintptr_t World = 0;
        uintptr_t PlayerController = 0;
        uintptr_t GameEngine = 0;
        uintptr_t GameInstance = 0;
        uintptr_t GameViewportClient = 0;
        uintptr_t LocalPlayer = 0;
        uintptr_t SenseStimulusComponent = 0;
        uintptr_t IRRBaseCharacter = 0;
        uintptr_t IRRAIBaseCharacter = 0;
        uintptr_t GeneralGameInstance = 0;
        uintptr_t EBBarrel = 0;
        uintptr_t IRRBodyComponent = 0;
        uintptr_t FirstPersonStamina = 0;
        uintptr_t FirstPersonStaminaArm = 0;
        uintptr_t FirstPersonWeaponRecoil = 0;
        uintptr_t InventoryComponent = 0;
        uintptr_t SimpleGameplayAttribute = 0;
        uintptr_t IRRTeamComponent = 0;
        uintptr_t WeaponComponent = 0;
        uintptr_t BPMasterWeapon = 0;
    };

    struct ClassPair
    {
        uintptr_t ObjectClass = 0;
        uintptr_t TargetClass = 0;
        bool operator==(const ClassPair& other) const
        {
            return ObjectClass == other.ObjectClass && TargetClass == other.TargetClass;
        }
    };

    struct ClassPairHash
    {
        size_t operator()(const ClassPair& value) const
        {
            const size_t first = std::hash<uintptr_t>{}(value.ObjectClass);
            const size_t second = std::hash<uintptr_t>{}(value.TargetClass);
            return first ^ (second + 0x9E3779B9u + (first << 6) + (first >> 2));
        }
    };

    struct PoseCacheEntry
    {
        std::array<FVector, 9> Parts{};
        // Engine-owned output allocation retained and passed back into the next
        // GetMainBoneLocations call. This bounds allocation to one small buffer per
        // sampled actor instead of leaking a new reflected TArray every refresh.
        TArray<FVector> EngineMainBones{};
        FVector ActorLocation{};
        uintptr_t BodyComponent = 0;
        ULONGLONG SampledAt = 0;
        uint16_t ValidMask = 0;
        bool UsedIndividualFallback = false;
    };

    struct VisibilityCacheEntry
    {
        FVector ExposedPoint{};
        FVector ActorLocation{};
        ULONGLONG SampledAt = 0;
        bool Visible = false;
    };

    struct VisibilityWork
    {
        uintptr_t Actor = 0;
        FVector ActorLocation{};
        FVector ObserverLocation{};
        std::array<FVector, 10> Points{};
        uint8_t PointCount = 0;
        float BroadRadius = 75.0f;
    };

    GameAccess::RuntimeDiagnostics g_diag{};
    ObjectArrayLayout g_objects{};
    ClassPointers g_classes{};
    std::vector<uintptr_t> g_allObjects;
    std::vector<uintptr_t> g_worldObjects;
    std::vector<uintptr_t> g_gameEngineObjects;
    std::vector<uintptr_t> g_viewportObjects;
    std::vector<uintptr_t> g_localPlayerObjects;
    std::vector<uintptr_t> g_controllerObjects;
    std::vector<uintptr_t> g_generalGameInstanceObjects;
    std::vector<uintptr_t> g_characterObjects;
    std::vector<uintptr_t> g_inventoryComponentObjects;
    std::vector<uintptr_t> g_weaponComponentObjects;
    std::vector<uintptr_t> g_weaponObjects;
    std::vector<uintptr_t> g_staminaObjects;
    std::vector<uintptr_t> g_staminaArmObjects;
    std::vector<uintptr_t> g_weaponRecoilObjects;
    std::vector<uintptr_t> g_staminaAttributes;
    std::vector<uintptr_t> g_actors;
    std::vector<uintptr_t> g_characters;
    std::unordered_set<uintptr_t> g_hostileCharacters;
    std::unordered_map<uintptr_t, PoseCacheEntry> g_poseCache;
    std::unordered_set<uintptr_t> g_poseQueuedActors;
    std::mutex g_poseCacheMutex;
    std::atomic<bool> g_poseTaskPending{ false };
    std::atomic<uint32_t> g_poseLastSampleThreadId{ 0 };
    std::atomic<int32_t> g_poseLastRequestedActors{ 0 };
    std::atomic<int32_t> g_poseLastSampledActors{ 0 };
    std::atomic<int32_t> g_poseLastAggregateActors{ 0 };
    std::atomic<int32_t> g_poseLastFallbackActors{ 0 };
    std::atomic<ULONGLONG> g_poseLastCompletedAt{ 0 };
    ULONGLONG g_lastPoseRequestAt = 0;
    uintptr_t g_poseCacheWorld = 0;
    std::unordered_map<uintptr_t, VisibilityCacheEntry> g_visibilityCache;
    std::vector<VisibilityWork> g_visibilityQueue;
    std::mutex g_visibilityCacheMutex;
    std::atomic<bool> g_visibilityTaskPending{ false };
    std::atomic<uint32_t> g_visibilityLastSampleThreadId{ 0 };
    std::atomic<int32_t> g_visibilityLastRequestedActors{ 0 };
    std::atomic<int32_t> g_visibilityLastVisibleActors{ 0 };
    std::atomic<ULONGLONG> g_visibilityLastCompletedAt{ 0 };
    ULONGLONG g_lastVisibilityRequestAt = 0;
    uintptr_t g_visibilityCacheWorld = 0;
    std::unordered_map<ClassPair, bool, ClassPairHash> g_isAResultCache;
    std::unordered_map<uintptr_t, uint16_t> g_classKindCache;
    ULONGLONG g_lastObjectRefresh = 0;
    uint64_t g_serial = 0;
    uintptr_t g_discoveredObjectArrayRoot = 0;
    bool g_objectArrayUsedSectionScan = false;
    int32_t g_objectArrayProbeScore = 0;
    uintptr_t g_processEventAddress = 0;
    uintptr_t g_lastFunctionObject = 0;
    int32_t g_lastFunctionIndex = -1;
    bool g_processEventValid = false;
    bool g_lastProcessEventCallSucceeded = false;

    struct RuntimeLogSnapshot
    {
        std::string FailureStage;
        uintptr_t ObjectArray = 0;
        int32_t ObjectCount = 0;
        int32_t ObjectProbeScore = 0;
        uintptr_t World = 0;
        uintptr_t GameInstance = 0;
        uintptr_t LocalPlayer = 0;
        uintptr_t Controller = 0;
        uintptr_t Pawn = 0;
        uintptr_t Weapon = 0;
        int32_t Characters = 0;
    };

    RuntimeLogSnapshot g_lastRuntimeLog{};
    ULONGLONG g_lastRuntimeLogTime = 0;
    bool g_hasRuntimeLog = false;

    bool ClassIsChildOf(uintptr_t objectClass, uintptr_t targetClass);
    bool IsObjectOfClass(uintptr_t object, uintptr_t targetClass);

    bool IsLiveActor(uintptr_t actor)
    {
        if (!actor || !Memory::IsReadable(actor + Offsets::AActor_Flags0, 1) ||
            !Memory::IsReadable(actor + Offsets::AActor_Flags5, 1))
            return false;
        uint8_t flags0 = 0;
        uint8_t flags5 = 0;
        if (!Memory::TryRead(actor + Offsets::AActor_Flags0, flags0) ||
            !Memory::TryRead(actor + Offsets::AActor_Flags5, flags5) ||
            (flags0 & 0x40u) != 0 || // AActor::bHidden
            (flags5 & 0x02u) != 0)   // AActor::bActorIsBeingDestroyed
            return false;
        const uintptr_t root = Memory::Read<uintptr_t>(actor +
            Offsets::AActor_RootComponent);
        return root && Memory::IsReadable(root +
            Offsets::USceneComponent_RelativeLocation, sizeof(FVector));
    }

    bool ReadBodyPoseDirect(uintptr_t actor, const PoseCacheEntry* previous,
                            PoseCacheEntry& out)
    {
        out = {};
        if (!actor || !GameAccess::IsIRRCharacter(actor) ||
            !GameAccess::GetActorLocation(actor, out.ActorLocation))
            return false;

        const uintptr_t body = Memory::Read<uintptr_t>(actor +
            Offsets::IRRBaseCharacter_BodyComponent);
        if (!body || !IsObjectOfClass(body, g_classes.IRRBodyComponent))
            return false;
        out.BodyComponent = body;

        auto plausible = [&](const FVector& point)
        {
            return point.IsFinite() && point.Distance(out.ActorLocation) < 275.0;
        };

        // This native function returns all main body anchors in one call. The old
        // implementation invoked nine separate functions per actor/sample, which
        // caused a large frame-time spike when many live pose targets were sampled.
        struct MainBoneParams
        {
            TArray<FVector> OutLocations{};
        } mainParams{};
        if (previous && previous->EngineMainBones.IsSane(64))
            mainParams.OutLocations = previous->EngineMainBones;
        const bool mainCallSucceeded = GameAccess::InvokeFunctionRaw(body,
                FunctionIndices::IRRBodyComponent_GetMainBoneLocations,
                &mainParams, sizeof(mainParams));
        out.EngineMainBones = mainParams.OutLocations;
        if (mainCallSucceeded)
        {
            const auto& locations = mainParams.OutLocations;
            const uintptr_t data = reinterpret_cast<uintptr_t>(locations.Data);
            if (locations.IsSane(64) && data && locations.Count >= 5 &&
                Memory::IsReadable(data, static_cast<size_t>(locations.Count) *
                    sizeof(FVector)))
            {
                const int32_t count = std::min<int32_t>(locations.Count, 9);
                std::array<FVector, 9> returned{};
                std::array<bool, 9> returnedValid{};
                for (int32_t index = 0; index < count; ++index)
                {
                    FVector point{};
                    if (!Memory::TryRead(data + static_cast<uintptr_t>(index) *
                            sizeof(FVector), point) || !plausible(point))
                        continue;
                    returned[static_cast<size_t>(index)] = point;
                    returnedValid[static_cast<size_t>(index)] = true;
                }

                // TMap-backed native output order is not assumed. After the first
                // individually-labelled sample, associate aggregate points with the
                // same actor's previous roles by nearest translated position. This
                // keeps left/right limbs and their parent links bound to one enemy.
                if (previous && previous->ValidMask)
                {
                    const FVector actorDelta = out.ActorLocation -
                        previous->ActorLocation;
                    std::array<bool, 9> used{};
                    for (int32_t role = 0; role < 9; ++role)
                    {
                        if ((previous->ValidMask &
                                static_cast<uint16_t>(1u << role)) == 0)
                            continue;
                        const FVector expected = previous->Parts[
                            static_cast<size_t>(role)] + actorDelta;
                        int32_t bestIndex = -1;
                        double bestDistance = std::numeric_limits<double>::max();
                        for (int32_t index = 0; index < count; ++index)
                        {
                            if (used[static_cast<size_t>(index)] ||
                                !returnedValid[static_cast<size_t>(index)])
                                continue;
                            const double distance = expected.Distance(
                                returned[static_cast<size_t>(index)]);
                            if (distance < bestDistance)
                            {
                                bestDistance = distance;
                                bestIndex = index;
                            }
                        }
                        if (bestIndex < 0 || bestDistance > 175.0)
                            continue;
                        used[static_cast<size_t>(bestIndex)] = true;
                        out.Parts[static_cast<size_t>(role)] = returned[
                            static_cast<size_t>(bestIndex)];
                        out.ValidMask |= static_cast<uint16_t>(1u << role);
                    }
                }
                out.SampledAt = GetTickCount64();
                double minZ = std::numeric_limits<double>::max();
                double maxZ = -std::numeric_limits<double>::max();
                for (int32_t index = 0; index < count; ++index)
                {
                    if ((out.ValidMask & static_cast<uint16_t>(1u << index)) == 0)
                        continue;
                    minZ = std::min(minZ, out.Parts[static_cast<size_t>(index)].Z);
                    maxZ = std::max(maxZ, out.Parts[static_cast<size_t>(index)].Z);
                }
                const double heightSpan = maxZ - minZ;
                if ((out.ValidMask & 0x007u) == 0x007u &&
                    (out.ValidMask & 0x1F8u) != 0 &&
                    std::isfinite(heightSpan) && heightSpan > 40.0 &&
                    heightSpan < 300.0)
                    return true;
            }
        }

        // Compatibility path for a build where the aggregate function is absent or
        // returns a different layout. It is only reached after the single-call path
        // fails validation.
        out.ValidMask = 0;
        out.UsedIndividualFallback = true;
        alignas(8) uint8_t eyeParams[0x18]{};
        if (GameAccess::InvokeFunctionRaw(body,
                FunctionIndices::IRRBodyComponent_GetEyeLocation,
                eyeParams, sizeof(eyeParams)))
        {
            FVector eye{};
            std::memcpy(&eye, eyeParams, sizeof(eye));
            if (plausible(eye))
            {
                out.Parts[0] = eye;
                out.ValidMask |= 1u;
            }
        }

        for (uint8_t part = 1; part <= 8; ++part)
        {
            alignas(8) uint8_t params[0x20]{};
            params[0] = part;
            if (!GameAccess::InvokeFunctionRaw(body,
                    FunctionIndices::IRRBodyComponent_GetBodyPartLocation,
                    params, sizeof(params)))
                continue;
            FVector point{};
            std::memcpy(&point, params + 0x08, sizeof(point));
            if (!plausible(point))
                continue;
            out.Parts[part] = point;
            out.ValidMask |= static_cast<uint16_t>(1u << part);
        }

        out.SampledAt = GetTickCount64();
        return (out.ValidMask & 0x007u) == 0x007u &&
               (out.ValidMask & 0x1F8u) != 0;
    }

    bool PoseTargetFromEntry(const PoseCacheEntry& entry,
                             const std::string& targetName,
                             const FVector& locationDelta,
                             FVector& out)
    {
        auto get = [&](int index, FVector& point)
        {
            if (index < 0 || index >= 9 ||
                (entry.ValidMask & static_cast<uint16_t>(1u << index)) == 0)
                return false;
            point = entry.Parts[static_cast<size_t>(index)] + locationDelta;
            return point.IsFinite();
        };

        if (targetName == "head") return get(0, out);
        if (targetName == "neck")
        {
            FVector eye{}, thorax{};
            if (!get(0, eye) || !get(1, thorax))
                return false;
            out = thorax + (eye - thorax) * 0.68;
            return out.IsFinite();
        }
        if (targetName == "chest") return get(1, out);
        if (targetName == "stomach" || targetName == "pelvis") return get(2, out);
        if (targetName == "arm_r" || targetName == "hand_r") return get(3, out);
        if (targetName == "arm_l" || targetName == "hand_l") return get(4, out);
        if (targetName == "leg_r") return get(5, out);
        if (targetName == "leg_l") return get(6, out);
        if (targetName == "foot_r") return get(7, out);
        if (targetName == "foot_l") return get(8, out);
        return false;
    }

    enum ClassKind : uint16_t
    {
        KindWorld = 1u << 0,
        KindGameEngine = 1u << 1,
        KindViewport = 1u << 2,
        KindLocalPlayer = 1u << 3,
        KindController = 1u << 4,
        KindGeneralGameInstance = 1u << 5,
        KindCharacter = 1u << 6,
        KindInventoryComponent = 1u << 7,
        KindWeaponComponent = 1u << 8,
        KindWeapon = 1u << 9,
        KindStamina = 1u << 10,
        KindStaminaArm = 1u << 11,
        KindWeaponRecoil = 1u << 12
    };

    uintptr_t ReadTrustedObjectClass(uintptr_t object)
    {
        if (!object)
            return 0;
#ifdef _MSC_VER
        __try
        {
            return *reinterpret_cast<const uintptr_t*>(
                object + Offsets::UObject_ClassPrivate);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
#else
        return Memory::Read<uintptr_t>(object + Offsets::UObject_ClassPrivate);
#endif
    }

    bool ReadArrayHeader(uintptr_t address, uintptr_t& data, int32_t& count, int32_t& capacity,
                         int32_t hardLimit)
    {
        const auto array = Memory::Read<TArray<uintptr_t>>(address);
        data = reinterpret_cast<uintptr_t>(array.Data);
        count = array.Count;
        capacity = array.Max;
        return array.IsSane(hardLimit) &&
               (array.Count == 0 || (data && Memory::IsReadable(data, sizeof(uintptr_t))));
    }

    uintptr_t GetObjectAt(const ObjectArrayLayout& layout, int32_t index)
    {
        if (!layout.Valid || index < 0 || index >= layout.Count)
            return 0;

        uintptr_t items = layout.Chunks;
        int32_t within = index;
        if (layout.Chunked)
        {
            constexpr int32_t ObjectsPerChunk = 64 * 1024;
            const int32_t chunkIndex = index / ObjectsPerChunk;
            within = index % ObjectsPerChunk;
            if (chunkIndex < 0 || chunkIndex >= layout.NumChunks)
                return 0;
            items = Memory::Read<uintptr_t>(layout.Chunks +
                static_cast<uintptr_t>(chunkIndex) * sizeof(uintptr_t));
        }
        return items ? Memory::Read<uintptr_t>(items +
            static_cast<uintptr_t>(within) * layout.ItemStride) : 0;
    }

    int ProbeScore(const ObjectArrayLayout& layout)
    {
        static constexpr int32_t KnownIndices[] = {
            0x0001, ObjectIndices::UWorldClass, ObjectIndices::APlayerControllerClass,
            ObjectIndices::UGameInstanceClass, ObjectIndices::IRRBaseCharacterClass
        };

        int score = 0;
        for (const int32_t index : KnownIndices)
        {
            const uintptr_t object = GetObjectAt(layout, index);
            if (!object)
                continue;
            const int32_t internalIndex = Memory::Read<int32_t>(
                object + Offsets::UObject_InternalIndex);
            const uintptr_t vtable = Memory::Read<uintptr_t>(object);
            if (internalIndex == index && vtable >= Memory::GetBase() &&
                vtable < Memory::GetBase() + Memory::GetModuleSize())
                ++score;
        }
        return score;
    }

    void ConsiderObjectArrayRoot(uintptr_t root, int& bestScore,
                                 ObjectArrayLayout& best)
    {
        if (!root || !Memory::IsReadable(root, 0x20))
            return;
        for (const uintptr_t objectOffset :
             { uintptr_t(0x0), uintptr_t(0x10), uintptr_t(0x18) })
        {
            const uintptr_t objectArray = root + objectOffset;
            const uintptr_t items = Memory::Read<uintptr_t>(objectArray);
            const int32_t capacity = Memory::Read<int32_t>(objectArray + 0x10);
            const int32_t count = Memory::Read<int32_t>(objectArray + 0x14);
            const int32_t maxChunks = Memory::Read<int32_t>(objectArray + 0x18);
            const int32_t numChunks = Memory::Read<int32_t>(objectArray + 0x1C);
            if (!items || count < 1024 || count > 2000000 || capacity < count ||
                capacity > 4000000)
                continue;

            for (const int32_t stride : { 0x18, 0x20 })
            {
                if (numChunks > 0 && numChunks <= maxChunks && maxChunks <= 512)
                {
                    ObjectArrayLayout candidate{
                        objectArray, items, count, capacity, numChunks, stride, true, true
                    };
                    const int score = ProbeScore(candidate);
                    if (score > bestScore) { bestScore = score; best = candidate; }
                }

                ObjectArrayLayout direct{
                    objectArray, items, count, capacity, 1, stride, false, true
                };
                const int score = ProbeScore(direct);
                if (score > bestScore) { bestScore = score; best = direct; }
            }
        }
    }

    void ScanWritableSectionsForObjectArray(int& bestScore, ObjectArrayLayout& best)
    {
        const uintptr_t base = Memory::GetBase();
        if (!base)
            return;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return;

        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (WORD sectionIndex = 0; sectionIndex < nt->FileHeader.NumberOfSections;
             ++sectionIndex, ++section)
        {
            if (!(section->Characteristics & IMAGE_SCN_MEM_READ) ||
                !(section->Characteristics & IMAGE_SCN_MEM_WRITE))
                continue;
            const uintptr_t start = base + section->VirtualAddress;
            const size_t size = static_cast<size_t>(section->Misc.VirtualSize);
            if (!size || !Memory::IsReadable(start, size))
                continue;

            const uintptr_t end = start + size;
            for (uintptr_t address = (start + 7u) & ~uintptr_t(7u);
                 address + 0x20 <= end; address += sizeof(uintptr_t))
            {
                uintptr_t items = 0;
                int32_t capacity = 0;
                int32_t count = 0;
                int32_t maxChunks = 0;
                int32_t numChunks = 0;
                std::memcpy(&items, reinterpret_cast<const void*>(address), sizeof(items));
                std::memcpy(&capacity, reinterpret_cast<const void*>(address + 0x10), sizeof(capacity));
                std::memcpy(&count, reinterpret_cast<const void*>(address + 0x14), sizeof(count));
                std::memcpy(&maxChunks, reinterpret_cast<const void*>(address + 0x18), sizeof(maxChunks));
                std::memcpy(&numChunks, reinterpret_cast<const void*>(address + 0x1C), sizeof(numChunks));
                if (!items || count < 1024 || count > 2000000 || capacity < count ||
                    capacity > 4000000 || numChunks <= 0 || numChunks > maxChunks ||
                    maxChunks > 512)
                    continue;

                for (const int32_t stride : { 0x18, 0x20 })
                {
                    ObjectArrayLayout candidate{
                        address, items, count, capacity, numChunks, stride, true, true
                    };
                    const int score = ProbeScore(candidate);
                    if (score > bestScore) { bestScore = score; best = candidate; }
                }
            }
        }
    }

    bool ProbeObjectArray()
    {
        g_objects = {};
        g_objectArrayUsedSectionScan = false;
        g_objectArrayProbeScore = 0;
        const uintptr_t address = Memory::ResolveRva(Offsets::GObjects);
        if (!address)
            return false;

        std::vector<uintptr_t> roots{ address };
        if (g_discoveredObjectArrayRoot && g_discoveredObjectArrayRoot != address)
            roots.push_back(g_discoveredObjectArrayRoot);
        const uintptr_t indirect = Memory::Read<uintptr_t>(address);
        if (indirect && Memory::IsReadable(indirect, 0x40))
            roots.push_back(indirect);

        int bestScore = 0;
        ObjectArrayLayout best{};
        for (const uintptr_t root : roots)
            ConsiderObjectArrayRoot(root, bestScore, best);
        if (bestScore < 3)
        {
            ScanWritableSectionsForObjectArray(bestScore, best);
            g_objectArrayUsedSectionScan = bestScore >= 3;
        }
        if (bestScore < 3)
        {
            g_objectArrayProbeScore = bestScore;
            return false;
        }
        g_objects = best;
        g_discoveredObjectArrayRoot = best.Root;
        g_objectArrayProbeScore = bestScore;
        if (best.Root != address)
            g_objectArrayUsedSectionScan = true;
        return true;
    }

    void LoadClassPointers()
    {
        if (!g_objects.Valid) { g_classes = {}; return; }
        g_classes.Function = GetObjectAt(g_objects, ObjectIndices::UFunctionClass);
        g_classes.Level = GetObjectAt(g_objects, ObjectIndices::ULevelClass);
        g_classes.World = GetObjectAt(g_objects, ObjectIndices::UWorldClass);
        g_classes.PlayerController = GetObjectAt(g_objects, ObjectIndices::APlayerControllerClass);
        g_classes.GameEngine = GetObjectAt(g_objects, ObjectIndices::UGameEngineClass);
        g_classes.GameInstance = GetObjectAt(g_objects, ObjectIndices::UGameInstanceClass);
        g_classes.GameViewportClient = GetObjectAt(g_objects, ObjectIndices::UGameViewportClientClass);
        g_classes.LocalPlayer = GetObjectAt(g_objects, ObjectIndices::ULocalPlayerClass);
        g_classes.SenseStimulusComponent = GetObjectAt(g_objects, ObjectIndices::SenseStimulusComponentClass);
        g_classes.IRRBaseCharacter = GetObjectAt(g_objects, ObjectIndices::IRRBaseCharacterClass);
        g_classes.IRRAIBaseCharacter = GetObjectAt(g_objects, ObjectIndices::IRRAIBaseCharacterClass);
        g_classes.GeneralGameInstance = GetObjectAt(g_objects, ObjectIndices::GeneralGameInstanceClass);
        g_classes.EBBarrel = GetObjectAt(g_objects, ObjectIndices::EBBarrelClass);
        g_classes.IRRBodyComponent = GetObjectAt(g_objects, ObjectIndices::IRRBodyComponentClass);
        g_classes.FirstPersonStamina = GetObjectAt(g_objects, ObjectIndices::FirstPersonStaminaClass);
        g_classes.FirstPersonStaminaArm = GetObjectAt(g_objects, ObjectIndices::FirstPersonStaminaArmClass);
        g_classes.FirstPersonWeaponRecoil = GetObjectAt(g_objects, ObjectIndices::FirstPersonWeaponRecoilClass);
        g_classes.InventoryComponent = GetObjectAt(g_objects, ObjectIndices::InventoryComponentClass);
        g_classes.SimpleGameplayAttribute = GetObjectAt(g_objects, ObjectIndices::SimpleGameplayAttributeClass);
        g_classes.IRRTeamComponent = GetObjectAt(g_objects, ObjectIndices::IRRTeamComponentClass);
        g_classes.WeaponComponent = GetObjectAt(g_objects, ObjectIndices::WeaponComponentClass);
        g_classes.BPMasterWeapon = GetObjectAt(g_objects, ObjectIndices::BPMasterWeaponClass);
    }

    void EnumerateObjects()
    {
        g_allObjects.clear();
        if (!g_objects.Valid)
            return;
        g_allObjects.reserve(static_cast<size_t>(g_objects.Count));
        constexpr int32_t ObjectsPerChunk = 64 * 1024;
        int32_t remaining = g_objects.Count;
        int32_t baseIndex = 0;
        const int32_t chunkCount = g_objects.Chunked ? g_objects.NumChunks : 1;
        for (int32_t chunkIndex = 0; chunkIndex < chunkCount && remaining > 0; ++chunkIndex)
        {
            const int32_t itemsInChunk = g_objects.Chunked ?
                std::min(remaining, ObjectsPerChunk) : remaining;
            const uintptr_t items = g_objects.Chunked ?
                Memory::Read<uintptr_t>(g_objects.Chunks +
                    static_cast<uintptr_t>(chunkIndex) * sizeof(uintptr_t)) :
                g_objects.Chunks;
            const size_t byteCount = static_cast<size_t>(itemsInChunk) *
                static_cast<size_t>(g_objects.ItemStride);
            std::vector<uint8_t> copy(byteCount);
            if (items && Memory::ReadRaw(items, copy.data(), copy.size()))
            {
                for (int32_t i = 0; i < itemsInChunk; ++i)
                {
                    uintptr_t object = 0;
                    std::memcpy(&object, copy.data() +
                        static_cast<size_t>(i) * g_objects.ItemStride, sizeof(object));
                    if (object)
                        g_allObjects.push_back(object);
                }
            }
            else
            {
                for (int32_t i = 0; i < itemsInChunk; ++i)
                {
                    const uintptr_t object = GetObjectAt(g_objects, baseIndex + i);
                    if (object)
                        g_allObjects.push_back(object);
                }
            }
            baseIndex += itemsInChunk;
            remaining -= itemsInChunk;
        }
    }

    void ClassifyObjects()
    {
        g_worldObjects.clear();
        g_gameEngineObjects.clear();
        g_viewportObjects.clear();
        g_localPlayerObjects.clear();
        g_controllerObjects.clear();
        g_generalGameInstanceObjects.clear();
        g_characterObjects.clear();
        g_inventoryComponentObjects.clear();
        g_weaponComponentObjects.clear();
        g_weaponObjects.clear();
        g_staminaObjects.clear();
        g_staminaArmObjects.clear();
        g_weaponRecoilObjects.clear();
        g_staminaAttributes.clear();

        for (const uintptr_t object : g_allObjects)
        {
            const uintptr_t objectClass = ReadTrustedObjectClass(object);
            if (!objectClass)
                continue;

            uint16_t kind = 0;
            const auto cached = g_classKindCache.find(objectClass);
            if (cached != g_classKindCache.end())
            {
                kind = cached->second;
            }
            else
            {
                if (ClassIsChildOf(objectClass, g_classes.World)) kind |= KindWorld;
                if (ClassIsChildOf(objectClass, g_classes.GameEngine)) kind |= KindGameEngine;
                if (ClassIsChildOf(objectClass, g_classes.GameViewportClient)) kind |= KindViewport;
                if (ClassIsChildOf(objectClass, g_classes.LocalPlayer)) kind |= KindLocalPlayer;
                if (ClassIsChildOf(objectClass, g_classes.PlayerController)) kind |= KindController;
                if (ClassIsChildOf(objectClass, g_classes.GeneralGameInstance)) kind |= KindGeneralGameInstance;
                if (ClassIsChildOf(objectClass, g_classes.IRRBaseCharacter)) kind |= KindCharacter;
                if (ClassIsChildOf(objectClass, g_classes.InventoryComponent)) kind |= KindInventoryComponent;
                if (ClassIsChildOf(objectClass, g_classes.WeaponComponent)) kind |= KindWeaponComponent;
                if (ClassIsChildOf(objectClass, g_classes.BPMasterWeapon)) kind |= KindWeapon;
                if (ClassIsChildOf(objectClass, g_classes.FirstPersonStamina)) kind |= KindStamina;
                if (ClassIsChildOf(objectClass, g_classes.FirstPersonStaminaArm)) kind |= KindStaminaArm;
                if (ClassIsChildOf(objectClass, g_classes.FirstPersonWeaponRecoil)) kind |= KindWeaponRecoil;
                g_classKindCache.emplace(objectClass, kind);
            }

            if (kind & KindWorld)
                g_worldObjects.push_back(object);
            else if (kind & KindGameEngine)
                g_gameEngineObjects.push_back(object);
            else if (kind & KindViewport)
                g_viewportObjects.push_back(object);
            else if (kind & KindLocalPlayer)
                g_localPlayerObjects.push_back(object);
            else if (kind & KindController)
                g_controllerObjects.push_back(object);
            else if (kind & KindGeneralGameInstance)
                g_generalGameInstanceObjects.push_back(object);
            else if (kind & KindCharacter)
                g_characterObjects.push_back(object);
            else if (kind & KindInventoryComponent)
                g_inventoryComponentObjects.push_back(object);
            else if (kind & KindWeaponComponent)
                g_weaponComponentObjects.push_back(object);
            else if (kind & KindWeapon)
                g_weaponObjects.push_back(object);
            if (kind & KindStamina)
                g_staminaObjects.push_back(object);
            if (kind & KindStaminaArm)
                g_staminaArmObjects.push_back(object);
            if (kind & KindWeaponRecoil)
                g_weaponRecoilObjects.push_back(object);
        }
    }

    void RefreshObjectsIfNeeded()
    {
        const ULONGLONG now = GetTickCount64();
        // Refresh frequently while acquiring the live chain (important when the loader
        // injects before a raid), then back off once a pawn is healthy so classifying
        // hundreds of thousands of UObjects cannot create periodic ESP hitches.
        const ULONGLONG interval = g_diag.Pawn ? 30000 : 5000;
        if (g_objects.Valid && !g_allObjects.empty() && now - g_lastObjectRefresh < interval)
            return;
        if (!ProbeObjectArray())
        {
            g_allObjects.clear();
            g_classes = {};
            g_lastObjectRefresh = now;
            return;
        }
        LoadClassPointers();
        EnumerateObjects();
        g_isAResultCache.clear();
        ClassifyObjects();
        g_lastObjectRefresh = now;
    }

    void LogRuntimeStateIfNeeded()
    {
        RuntimeLogSnapshot current{};
        current.FailureStage = g_diag.FailureStage ? g_diag.FailureStage : "unknown";
        current.ObjectArray = g_diag.ResolvedObjectArray;
        current.ObjectCount = g_diag.ObjectCount;
        current.ObjectProbeScore = g_diag.ObjectArrayProbeScore;
        current.World = g_diag.World;
        current.GameInstance = g_diag.GameInstance;
        current.LocalPlayer = g_diag.LocalPlayer;
        current.Controller = g_diag.PlayerController;
        current.Pawn = g_diag.Pawn;
        current.Weapon = g_diag.EquippedWeapon;
        current.Characters = g_diag.ActiveCharacterCount;

        const ULONGLONG now = GetTickCount64();
        const bool changed = !g_hasRuntimeLog ||
            current.FailureStage != g_lastRuntimeLog.FailureStage ||
            current.ObjectArray != g_lastRuntimeLog.ObjectArray ||
            current.ObjectCount != g_lastRuntimeLog.ObjectCount ||
            current.ObjectProbeScore != g_lastRuntimeLog.ObjectProbeScore ||
            current.World != g_lastRuntimeLog.World ||
            current.GameInstance != g_lastRuntimeLog.GameInstance ||
            current.LocalPlayer != g_lastRuntimeLog.LocalPlayer ||
            current.Controller != g_lastRuntimeLog.Controller ||
            current.Pawn != g_lastRuntimeLog.Pawn ||
            current.Weapon != g_lastRuntimeLog.Weapon ||
            current.Characters != g_lastRuntimeLog.Characters;
        if (!changed && now - g_lastRuntimeLogTime < 15000)
            return;

        DebugLog("[Runtime] stage=%s | GUObjectArray=0x%llX objects=%d probe=%d/5 source=%s | "
                 "World=0x%llX GI=0x%llX LocalPlayer=0x%llX Controller=0x%llX "
                 "Pawn=0x%llX Weapon=0x%llX | characters=%d\n",
            current.FailureStage.c_str(),
            static_cast<unsigned long long>(current.ObjectArray), current.ObjectCount,
            current.ObjectProbeScore,
            g_diag.ObjectArrayUsedSectionScan ? "PE-scan" : "configured-RVA",
            static_cast<unsigned long long>(current.World),
            static_cast<unsigned long long>(current.GameInstance),
            static_cast<unsigned long long>(current.LocalPlayer),
            static_cast<unsigned long long>(current.Controller),
            static_cast<unsigned long long>(current.Pawn),
            static_cast<unsigned long long>(current.Weapon), current.Characters);
        g_lastRuntimeLog = std::move(current);
        g_lastRuntimeLogTime = now;
        g_hasRuntimeLog = true;
    }

    bool ClassIsChildOf(uintptr_t objectClass, uintptr_t targetClass)
    {
        if (!objectClass || !targetClass)
            return false;
        if (objectClass == targetClass)
            return true;

        const ClassPair key{ objectClass, targetClass };
        const auto cached = g_isAResultCache.find(key);
        if (cached != g_isAResultCache.end())
            return cached->second;

        uintptr_t current = objectClass;
        bool result = false;
        std::unordered_set<uintptr_t> visited;
        for (int depth = 0; current && depth < 64; ++depth)
        {
            if (current == targetClass) { result = true; break; }
            if (!visited.insert(current).second)
                break;
            current = Memory::Read<uintptr_t>(current + Offsets::UStruct_SuperStruct);
        }
        g_isAResultCache[key] = result;
        return result;
    }

    bool IsObjectOfClass(uintptr_t object, uintptr_t targetClass)
    {
        if (!object || !targetClass ||
            !Memory::IsReadable(object + Offsets::UObject_ClassPrivate, sizeof(uintptr_t)))
            return false;
        return ClassIsChildOf(Memory::Read<uintptr_t>(
            object + Offsets::UObject_ClassPrivate), targetClass);
    }

    bool ObjectBelongsToWorld(uintptr_t object, uintptr_t world)
    {
        if (!object || !world)
            return false;
        uintptr_t outer = Memory::Read<uintptr_t>(object + Offsets::UObject_OuterPrivate);
        for (int depth = 0; outer && depth < 8; ++depth)
        {
            if (outer == world)
                return true;
            if (IsObjectOfClass(outer, g_classes.Level))
                return Memory::Read<uintptr_t>(outer + Offsets::ULevel_OwningWorld) == world;
            const uintptr_t next = Memory::Read<uintptr_t>(
                outer + Offsets::UObject_OuterPrivate);
            if (!next || next == outer)
                break;
            outer = next;
        }
        return false;
    }

    bool ObjectOwnedBy(uintptr_t object, uintptr_t owner, int maxDepth = 16)
    {
        if (!object || !owner)
            return false;
        uintptr_t outer = Memory::Read<uintptr_t>(object + Offsets::UObject_OuterPrivate);
        for (int depth = 0; outer && depth < maxDepth; ++depth)
        {
            if (outer == owner)
                return true;
            const uintptr_t next = Memory::Read<uintptr_t>(
                outer + Offsets::UObject_OuterPrivate);
            if (!next || next == outer)
                break;
            outer = next;
        }
        return false;
    }

    uintptr_t FindOwnedObject(const std::vector<uintptr_t>& objects, uintptr_t owner)
    {
        if (!owner)
            return 0;
        for (const uintptr_t object : objects)
            if (ObjectOwnedBy(object, owner))
                return object;
        return 0;
    }

    std::vector<uintptr_t> ReadPointerArray(uintptr_t address, int32_t hardLimit,
                                            uintptr_t* outData = nullptr,
                                            int32_t* outCount = nullptr,
                                            int32_t* outCapacity = nullptr)
    {
        std::vector<uintptr_t> result;
        uintptr_t data = 0;
        int32_t count = 0;
        int32_t capacity = 0;
        const bool valid = ReadArrayHeader(address, data, count, capacity, hardLimit);
        if (outData) *outData = data;
        if (outCount) *outCount = count;
        if (outCapacity) *outCapacity = capacity;
        if (!valid || count <= 0)
            return result;
        result.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i)
        {
            const uintptr_t value = Memory::Read<uintptr_t>(
                data + static_cast<uintptr_t>(i) * sizeof(uintptr_t));
            if (value)
                result.push_back(value);
        }
        return result;
    }

    struct WorldCandidate
    {
        uintptr_t World = 0;
        GameAccess::Source From = GameAccess::Source::None;
        int Score = std::numeric_limits<int>::min();
        int Characters = 0;
    };

    WorldCandidate EvaluateWorld(uintptr_t world, GameAccess::Source source,
                                 uintptr_t viewportWorld)
    {
        WorldCandidate result{ world, source, std::numeric_limits<int>::min(), 0 };
        if (!world || (g_classes.World && !IsObjectOfClass(world, g_classes.World)))
            return result;
        const uintptr_t level = Memory::Read<uintptr_t>(
            world + Offsets::UWorld_PersistentLevel);
        if (!level || (g_classes.Level && !IsObjectOfClass(level, g_classes.Level)))
            return result;

        int score = 50;
        if (world == viewportWorld) score += 300;
        if (source == GameAccess::Source::GWorld) score += 25;
        if (Memory::Read<uintptr_t>(level + Offsets::ULevel_OwningWorld) == world) score += 40;
        if (Memory::Read<uintptr_t>(world + Offsets::UWorld_OwningGameInstance)) score += 35;
        if (Memory::Read<uintptr_t>(world + Offsets::UWorld_GameState)) score += 20;
        if (Memory::Read<uintptr_t>(world + Offsets::UWorld_AuthorityGameMode)) score += 10;

        uintptr_t data = 0;
        int32_t count = 0;
        int32_t capacity = 0;
        if (ReadArrayHeader(level + Offsets::ULevel_Actors, data, count, capacity, 50000))
        {
            score += 30 + std::min(count, 500) / 10;
            for (int32_t i = 0; i < count; ++i)
            {
                const uintptr_t actor = Memory::Read<uintptr_t>(
                    data + static_cast<uintptr_t>(i) * sizeof(uintptr_t));
                if (IsObjectOfClass(actor, g_classes.IRRBaseCharacter))
                    ++result.Characters;
            }
            if (result.Characters > 0)
                score += 150 + std::min(result.Characters, 50) * 2;
        }
        result.Score = score;
        return result;
    }

    uintptr_t ChooseGameEngine()
    {
        uintptr_t fallback = 0;
        for (const uintptr_t object : g_gameEngineObjects)
        {
            if (!fallback) fallback = object;
            if (Memory::Read<uintptr_t>(object + Offsets::UEngine_GameViewport) ||
                Memory::Read<uintptr_t>(object + Offsets::UGameEngine_GameInstance))
                return object;
        }
        return fallback;
    }

    int ScoreGameInstance(uintptr_t gameInstance, uintptr_t preferredViewport)
    {
        if (!gameInstance || (g_classes.GameInstance &&
            !IsObjectOfClass(gameInstance, g_classes.GameInstance)))
            return std::numeric_limits<int>::min();
        int score = 10;
        uintptr_t data = 0;
        int32_t count = 0;
        int32_t capacity = 0;
        if (ReadArrayHeader(gameInstance + Offsets::UGameInstance_LocalPlayers,
                            data, count, capacity, 16))
        {
            score += 30;
            for (int32_t i = 0; i < count; ++i)
            {
                const uintptr_t player = Memory::Read<uintptr_t>(
                    data + static_cast<uintptr_t>(i) * sizeof(uintptr_t));
                if (!player)
                    continue;
                score += 50;
                if (Memory::Read<uintptr_t>(player + Offsets::ULocalPlayer_ViewportClient) ==
                    preferredViewport)
                    score += 100;
                if (Memory::Read<uintptr_t>(player + Offsets::UPlayer_PlayerController))
                    score += 100;
            }
        }
        if (IsObjectOfClass(gameInstance, g_classes.GeneralGameInstance))
            score += 25;
        return score;
    }

    uintptr_t FindComponent(uintptr_t owner, uintptr_t componentClass)
    {
        if (!owner || !componentClass)
            return 0;
        for (const uintptr_t offset : { Offsets::AActor_InstanceComponents,
                                        Offsets::AActor_BlueprintCreatedComponents })
        {
            for (const uintptr_t component : ReadPointerArray(owner + offset, 2048))
                if (IsObjectOfClass(component, componentClass))
                    return component;
        }

        const auto& candidates = componentClass == g_classes.InventoryComponent ?
            g_inventoryComponentObjects : g_weaponComponentObjects;
        for (const uintptr_t object : candidates)
        {
            uintptr_t outer = Memory::Read<uintptr_t>(object + Offsets::UObject_OuterPrivate);
            for (int depth = 0; outer && depth < 4; ++depth)
            {
                if (outer == owner)
                    return object;
                const uintptr_t next = Memory::Read<uintptr_t>(
                    outer + Offsets::UObject_OuterPrivate);
                if (!next || next == outer)
                    break;
                outer = next;
            }
        }
        return 0;
    }

    FVector RotateVector(const FVector& v, const FRotator& r)
    {
        constexpr double DegToRad = 3.14159265358979323846 / 180.0;
        const double p = r.Pitch * DegToRad;
        const double y = r.Yaw * DegToRad;
        const double roll = r.Roll * DegToRad;
        const double sp = std::sin(p), cp = std::cos(p);
        const double sy = std::sin(y), cy = std::cos(y);
        const double sr = std::sin(roll), cr = std::cos(roll);
        return {
            v.X * (cp * cy) + v.Y * (sr * sp * cy - cr * sy) +
                v.Z * (-(cr * sp * cy + sr * sy)),
            v.X * (cp * sy) + v.Y * (sr * sp * sy + cr * cy) +
                v.Z * (cy * sr - cr * sp * sy),
            v.X * sp + v.Y * (-sr * cp) + v.Z * (cr * cp)
        };
    }

    bool ComponentPointToWorld(uintptr_t component, const FVector& componentPoint,
                               FVector& out)
    {
        if (!component || !componentPoint.IsFinite())
            return false;
        FVector point = componentPoint;
        uintptr_t current = component;
        std::unordered_set<uintptr_t> visited;
        for (int depth = 0; current && depth < 16; ++depth)
        {
            if (!visited.insert(current).second)
                return false;
            FVector location{};
            FRotator rotation{};
            FVector scale{ 1.0, 1.0, 1.0 };
            if (!Memory::TryRead(current + Offsets::USceneComponent_RelativeLocation, location) ||
                !location.IsFinite() ||
                !Memory::TryRead(current + Offsets::USceneComponent_RelativeRotation, rotation) ||
                !rotation.IsFinite() ||
                !Memory::TryRead(current + Offsets::USceneComponent_RelativeScale3D, scale) ||
                !scale.IsFinite())
                return false;
            if (std::abs(scale.X) < 0.00001) scale.X = 1.0;
            if (std::abs(scale.Y) < 0.00001) scale.Y = 1.0;
            if (std::abs(scale.Z) < 0.00001) scale.Z = 1.0;
            point = RotateVector({ point.X * scale.X, point.Y * scale.Y,
                                   point.Z * scale.Z }, rotation) + location;
            if (!point.IsFinite())
                return false;
            const uintptr_t parent = Memory::Read<uintptr_t>(
                current + Offsets::USceneComponent_AttachParent);
            if (!parent || parent == current)
                break;
            current = parent;
        }
        out = point;
        return out.IsFinite();
    }

}

namespace GameAccess
{
    const char* SourceName(Source source)
    {
        switch (source)
        {
        case Source::GWorld: return "GWorld";
        case Source::Viewport: return "Viewport";
        case Source::GameEngine: return "GameEngine";
        case Source::World: return "World";
        case Source::LocalPlayers: return "LocalPlayers";
        case Source::ControllerPlayer: return "Controller::Player";
        case Source::ObjectArray: return "GUObjectArray scan";
        default: return "none";
        }
    }

    void Reset()
    {
        g_diag = {};
        g_objects = {};
        g_classes = {};
        g_allObjects.clear();
        g_worldObjects.clear();
        g_gameEngineObjects.clear();
        g_viewportObjects.clear();
        g_localPlayerObjects.clear();
        g_controllerObjects.clear();
        g_generalGameInstanceObjects.clear();
        g_characterObjects.clear();
        g_inventoryComponentObjects.clear();
        g_weaponComponentObjects.clear();
        g_weaponObjects.clear();
        g_staminaObjects.clear();
        g_staminaArmObjects.clear();
        g_weaponRecoilObjects.clear();
        g_actors.clear();
        g_characters.clear();
        g_hostileCharacters.clear();
        g_staminaAttributes.clear();
        {
            std::lock_guard<std::mutex> lock(g_poseCacheMutex);
            g_poseCache.clear();
            g_poseQueuedActors.clear();
            g_poseCacheWorld = 0;
        }
        g_poseTaskPending.store(false);
        g_poseLastSampleThreadId.store(0);
        g_poseLastRequestedActors.store(0);
        g_poseLastSampledActors.store(0);
        g_poseLastAggregateActors.store(0);
        g_poseLastFallbackActors.store(0);
        g_poseLastCompletedAt.store(0);
        g_lastPoseRequestAt = 0;
        {
            std::lock_guard<std::mutex> lock(g_visibilityCacheMutex);
            g_visibilityCache.clear();
            g_visibilityQueue.clear();
            g_visibilityCacheWorld = 0;
        }
        g_visibilityTaskPending.store(false);
        g_visibilityLastSampleThreadId.store(0);
        g_visibilityLastRequestedActors.store(0);
        g_visibilityLastVisibleActors.store(0);
        g_visibilityLastCompletedAt.store(0);
        g_lastVisibilityRequestAt = 0;
        g_isAResultCache.clear();
        g_classKindCache.clear();
        g_lastObjectRefresh = 0;
        g_discoveredObjectArrayRoot = 0;
        g_objectArrayUsedSectionScan = false;
        g_objectArrayProbeScore = 0;
        g_processEventAddress = 0;
        g_lastFunctionObject = 0;
        g_lastFunctionIndex = -1;
        g_processEventValid = false;
        g_lastProcessEventCallSucceeded = false;
        g_lastRuntimeLog = {};
        g_lastRuntimeLogTime = 0;
        g_hasRuntimeLog = false;
    }

    void Refresh()
    {
        RefreshObjectsIfNeeded();
        g_actors.clear();
        g_characters.clear();
        g_hostileCharacters.clear();
        g_diag = {};
        g_diag.Serial = ++g_serial;
        g_diag.ModuleBase = Memory::GetBase();
        g_diag.GWorldSlot = Memory::ResolveRva(Offsets::GWorld);
        g_diag.RawGWorld = g_diag.GWorldSlot ?
            Memory::Read<uintptr_t>(g_diag.GWorldSlot) : 0;
        g_diag.RawGWorldPlausible = g_diag.RawGWorld &&
            Memory::IsReadable(g_diag.RawGWorld, sizeof(uintptr_t));
        g_diag.GObjectsAddress = Memory::ResolveRva(Offsets::GObjects);
        g_diag.ResolvedObjectArray = g_objects.Root;
        g_diag.ObjectChunkTable = g_objects.Chunks;
        g_diag.ObjectCount = g_objects.Count;
        g_diag.ObjectCapacity = g_objects.Capacity;
        g_diag.ObjectItemStride = g_objects.ItemStride;
        g_diag.ObjectArrayValid = g_objects.Valid;
        g_diag.ObjectArrayUsedSectionScan = g_objectArrayUsedSectionScan;
        g_diag.ObjectArrayProbeScore = g_objectArrayProbeScore;
        g_diag.ObjectWorldCount = static_cast<int32_t>(g_worldObjects.size());
        g_diag.ObjectGameEngineCount = static_cast<int32_t>(g_gameEngineObjects.size());
        g_diag.ObjectViewportCount = static_cast<int32_t>(g_viewportObjects.size());
        g_diag.ObjectLocalPlayerCount = static_cast<int32_t>(g_localPlayerObjects.size());
        g_diag.ObjectControllerCount = static_cast<int32_t>(g_controllerObjects.size());
        g_diag.ObjectGameInstanceCount = static_cast<int32_t>(
            g_generalGameInstanceObjects.size());
        g_diag.ObjectCharacterCount = static_cast<int32_t>(g_characterObjects.size());
        g_diag.ObjectWeaponCount = static_cast<int32_t>(g_weaponObjects.size());
        g_diag.ProcessEventAddress = g_processEventAddress;
        g_diag.LastFunctionObject = g_lastFunctionObject;
        g_diag.LastFunctionIndex = g_lastFunctionIndex;
        g_diag.ProcessEventValid = g_processEventValid;
        g_diag.LastProcessEventCallSucceeded = g_lastProcessEventCallSucceeded;

        g_diag.GameEngine = ChooseGameEngine();
        if (g_diag.GameEngine)
        {
            g_diag.EngineGameInstance = Memory::Read<uintptr_t>(g_diag.GameEngine +
                Offsets::UGameEngine_GameInstance);
            g_diag.ViewportClient = Memory::Read<uintptr_t>(g_diag.GameEngine +
                Offsets::UEngine_GameViewport);
        }
        if (!g_diag.ViewportClient)
        {
            for (const uintptr_t object : g_viewportObjects)
            {
                if (Memory::Read<uintptr_t>(object + Offsets::UGameViewportClient_World) ||
                    Memory::Read<uintptr_t>(object + Offsets::UGameViewportClient_GameInstance))
                {
                    g_diag.ViewportClient = object;
                    break;
                }
            }
        }
        if (g_diag.ViewportClient)
        {
            g_diag.ViewportWorld = Memory::Read<uintptr_t>(g_diag.ViewportClient +
                Offsets::UGameViewportClient_World);
            g_diag.ViewportGameInstance = Memory::Read<uintptr_t>(
                g_diag.ViewportClient + Offsets::UGameViewportClient_GameInstance);
        }

        std::vector<WorldCandidate> worlds;
        std::unordered_set<uintptr_t> seenWorlds;
        auto addWorld = [&](uintptr_t world, Source source)
        {
            if (world && seenWorlds.insert(world).second)
                worlds.push_back(EvaluateWorld(world, source, g_diag.ViewportWorld));
        };
        // The viewport world is the frame-producing world. In a live raid it is both
        // the most reliable choice and the cheapest one; avoid rescanning every stale,
        // menu and transient UWorld on every Present once this route validates.
        addWorld(g_diag.ViewportWorld, Source::Viewport);
        const bool viewportRaid = !worlds.empty() && worlds.front().Characters > 0 &&
            worlds.front().Score != std::numeric_limits<int>::min();
        if (!viewportRaid)
        {
            addWorld(g_diag.RawGWorld, Source::GWorld);
            for (const uintptr_t object : g_worldObjects)
                addWorld(object, Source::ObjectArray);
        }
        g_diag.WorldCandidateCount = static_cast<int32_t>(worlds.size());
        // A menu/transient world can have a viewport, persistent level and game
        // instance, so those fields alone are not raid evidence. Only publish a
        // live World after its level contains actual IRR character instances.
        auto bestWorld = worlds.end();
        for (auto candidate = worlds.begin(); candidate != worlds.end(); ++candidate)
        {
            if (candidate->Characters <= 0 ||
                candidate->Score == std::numeric_limits<int>::min())
                continue;
            if (bestWorld == worlds.end() || candidate->Score > bestWorld->Score)
                bestWorld = candidate;
        }
        if (bestWorld != worlds.end())
        {
            g_diag.World = bestWorld->World;
            g_diag.WorldSource = bestWorld->From;
            g_diag.WorldIsActiveRaid = true;
        }

        if (g_diag.World)
        {
            g_diag.PersistentLevel = Memory::Read<uintptr_t>(g_diag.World +
                Offsets::UWorld_PersistentLevel);
            g_diag.WorldGameInstance = Memory::Read<uintptr_t>(g_diag.World +
                Offsets::UWorld_OwningGameInstance);
            if (g_diag.PersistentLevel)
            {
                g_diag.LevelOwningWorld = Memory::Read<uintptr_t>(
                    g_diag.PersistentLevel + Offsets::ULevel_OwningWorld);
                g_actors = ReadPointerArray(g_diag.PersistentLevel + Offsets::ULevel_Actors,
                    50000, &g_diag.ActorArrayData, &g_diag.ActorCount,
                    &g_diag.ActorCapacity);
            }
        }

        struct GameInstanceCandidate { uintptr_t Value; Source From; int Score; };
        std::vector<GameInstanceCandidate> gameInstances;
        std::unordered_set<uintptr_t> seenInstances;
        auto addInstance = [&](uintptr_t value, Source from, int sourceBonus)
        {
            if (!value || !seenInstances.insert(value).second)
                return;
            const int score = ScoreGameInstance(value, g_diag.ViewportClient);
            if (score > std::numeric_limits<int>::min())
                gameInstances.push_back({ value, from, score + sourceBonus });
        };
        addInstance(g_diag.WorldGameInstance, Source::World, 40);
        addInstance(g_diag.ViewportGameInstance, Source::Viewport, 80);
        addInstance(g_diag.EngineGameInstance, Source::GameEngine, 60);
        for (const uintptr_t object : g_generalGameInstanceObjects)
            addInstance(object, Source::ObjectArray, 0);
        const auto bestInstance = std::max_element(gameInstances.begin(), gameInstances.end(),
            [](const GameInstanceCandidate& a, const GameInstanceCandidate& b)
            { return a.Score < b.Score; });
        if (bestInstance != gameInstances.end())
        {
            g_diag.GameInstance = bestInstance->Value;
            g_diag.GameInstanceSource = bestInstance->From;
            g_diag.GameInstanceTypeValid = IsObjectOfClass(
                g_diag.GameInstance, g_classes.GeneralGameInstance);
        }

        if (g_diag.GameInstance)
        {
            const auto localPlayers = ReadPointerArray(g_diag.GameInstance +
                Offsets::UGameInstance_LocalPlayers, 16, &g_diag.LocalPlayersData,
                &g_diag.LocalPlayersCount, &g_diag.LocalPlayersCapacity);
            for (const uintptr_t player : localPlayers)
            {
                if (!g_classes.LocalPlayer || IsObjectOfClass(player, g_classes.LocalPlayer))
                {
                    g_diag.LocalPlayer = player;
                    g_diag.LocalPlayerSource = Source::LocalPlayers;
                    break;
                }
            }
        }
        if (!g_diag.LocalPlayer)
        {
            for (const uintptr_t object : g_localPlayerObjects)
            {
                const uintptr_t viewport = Memory::Read<uintptr_t>(object +
                    Offsets::ULocalPlayer_ViewportClient);
                const uintptr_t controller = Memory::Read<uintptr_t>(object +
                    Offsets::UPlayer_PlayerController);
                if ((g_diag.ViewportClient && viewport == g_diag.ViewportClient) || controller)
                {
                    g_diag.LocalPlayer = object;
                    g_diag.LocalPlayerSource = Source::ObjectArray;
                    break;
                }
            }
        }

        if (g_diag.LocalPlayer)
            g_diag.PlayerController = Memory::Read<uintptr_t>(g_diag.LocalPlayer +
                Offsets::UPlayer_PlayerController);
        if (g_diag.PlayerController &&
            IsObjectOfClass(g_diag.PlayerController, g_classes.PlayerController))
            g_diag.ControllerSource = Source::LocalPlayers;
        else
            g_diag.PlayerController = 0;

        for (const uintptr_t object : g_controllerObjects)
        {
            ++g_diag.ScannedControllerCount;
            if (g_diag.PlayerController)
                continue;
            const bool local = Memory::Read<bool>(object +
                Offsets::APlayerController_bIsLocalController);
            const uintptr_t player = Memory::Read<uintptr_t>(object +
                Offsets::APlayerController_Player);
            const uintptr_t camera = Memory::Read<uintptr_t>(object +
                Offsets::APlayerController_PlayerCameraManager);
            if (local || (player && player == g_diag.LocalPlayer) || (player && camera))
            {
                g_diag.PlayerController = object;
                g_diag.ControllerSource = Source::ObjectArray;
            }
        }

        if (g_diag.PlayerController)
        {
            g_diag.ControllerPlayer = Memory::Read<uintptr_t>(g_diag.PlayerController +
                Offsets::APlayerController_Player);
            if (!g_diag.LocalPlayer &&
                IsObjectOfClass(g_diag.ControllerPlayer, g_classes.LocalPlayer))
            {
                g_diag.LocalPlayer = g_diag.ControllerPlayer;
                g_diag.LocalPlayerSource = Source::ControllerPlayer;
            }
            g_diag.ControllerPawn = Memory::Read<uintptr_t>(g_diag.PlayerController +
                Offsets::AController_Pawn);
            g_diag.AcknowledgedPawn = Memory::Read<uintptr_t>(g_diag.PlayerController +
                Offsets::APlayerController_AcknowledgedPawn);
            g_diag.Pawn = g_diag.ControllerPawn ?
                g_diag.ControllerPawn : g_diag.AcknowledgedPawn;
            g_diag.CameraManager = Memory::Read<uintptr_t>(g_diag.PlayerController +
                Offsets::APlayerController_PlayerCameraManager);
        }
        g_diag.PawnTypeValid = IsObjectOfClass(g_diag.Pawn, g_classes.IRRBaseCharacter);
        if (g_diag.Pawn && g_classes.IRRBaseCharacter && !g_diag.PawnTypeValid)
            g_diag.Pawn = 0;

        // Character discovery does not depend on local-player acquisition. ULevel actors
        // are supplemented by active-world GUObjectArray instances when necessary.
        std::unordered_set<uintptr_t> seenCharacters;
        for (const uintptr_t actor : g_actors)
        {
            if (IsLiveActor(actor) &&
                IsObjectOfClass(actor, g_classes.IRRBaseCharacter) &&
                seenCharacters.insert(actor).second)
                g_characters.push_back(actor);
        }
        for (const uintptr_t object : g_characterObjects)
        {
            ++g_diag.ScannedCharacterCount;
            if (IsLiveActor(object) && ObjectBelongsToWorld(object, g_diag.World) &&
                seenCharacters.insert(object).second)
                g_characters.push_back(object);
        }
        g_diag.ActiveCharacterCount = static_cast<int32_t>(g_characters.size());
        if (!g_characters.empty())
            g_diag.WorldIsActiveRaid = true;

        // Use the game's own per-player hostility list when it is populated. The
        // list is transient during map travel and can legitimately be empty for a
        // few frames, so IsEnemyCharacter also falls back to the dump-validated AI
        // class instead of making the aimbot wait forever for a team array.
        if (g_diag.Pawn)
        {
            g_diag.LocalTeamComponent = Memory::Read<uintptr_t>(g_diag.Pawn +
                Offsets::IRRBaseCharacter_TeamComponent);
            g_diag.LocalTeamComponentTypeValid = IsObjectOfClass(
                g_diag.LocalTeamComponent, g_classes.IRRTeamComponent);
        }
        if (g_diag.LocalTeamComponentTypeValid)
        {
            uintptr_t data = 0;
            int32_t count = 0;
            int32_t capacity = 0;
            const bool headerValid = ReadArrayHeader(g_diag.LocalTeamComponent +
                Offsets::IRRTeamComponent_Hostiles, data, count, capacity, 4096);
            g_diag.HostileArrayData = data;
            g_diag.HostileArrayCount = count;
            g_diag.HostileArrayCapacity = capacity;

            int32_t nonNullEntries = 0;
            int32_t invalidEntries = 0;
            if (headerValid)
            {
                for (int32_t i = 0; i < count; ++i)
                {
                    const uintptr_t actor = Memory::Read<uintptr_t>(data +
                        static_cast<uintptr_t>(i) * sizeof(uintptr_t));
                    if (!actor)
                        continue;
                    ++nonNullEntries;
                    if (!IsObjectOfClass(actor, g_classes.IRRBaseCharacter))
                    {
                        ++invalidEntries;
                        continue;
                    }
                    if (seenCharacters.find(actor) != seenCharacters.end())
                        g_hostileCharacters.insert(actor);
                }
            }
            g_diag.HostileArrayValid = headerValid && invalidEntries == 0 &&
                (count == 0 || nonNullEntries > 0);
            g_diag.HostileCharacterCount = static_cast<int32_t>(
                g_hostileCharacters.size());
        }

        if (g_diag.Pawn)
        {
            const uintptr_t directWeapon = Memory::Read<uintptr_t>(g_diag.Pawn +
                Offsets::IRRBaseCharacter_WeaponInHands);
            if (directWeapon && (!g_classes.BPMasterWeapon ||
                IsObjectOfClass(directWeapon, g_classes.BPMasterWeapon)))
                g_diag.EquippedWeapon = directWeapon;
        }
        if (!g_diag.EquippedWeapon)
        {
            for (const uintptr_t object : g_weaponObjects)
            {
                const uintptr_t owner = Memory::Read<uintptr_t>(object + Offsets::AActor_Owner);
                if (g_diag.Pawn && owner == g_diag.Pawn)
                {
                    g_diag.EquippedWeapon = object;
                    break;
                }
            }
        }
        g_diag.WeaponTypeValid = IsObjectOfClass(
            g_diag.EquippedWeapon, g_classes.BPMasterWeapon);
        if (g_diag.EquippedWeapon)
        {
            const uintptr_t directComponent = Memory::Read<uintptr_t>(
                g_diag.EquippedWeapon + Offsets::BPMasterWeapon_WeaponComponent);
            if (IsObjectOfClass(directComponent, g_classes.WeaponComponent))
                g_diag.WeaponComponent = directComponent;
            if (!g_diag.WeaponComponent)
                g_diag.WeaponComponent = FindComponent(
                    g_diag.EquippedWeapon, g_classes.WeaponComponent);
        }
        g_diag.WeaponComponentTypeValid = IsObjectOfClass(
            g_diag.WeaponComponent, g_classes.WeaponComponent);
        if (g_diag.EquippedWeapon)
        {
            const uintptr_t barrel = Memory::Read<uintptr_t>(g_diag.EquippedWeapon +
                Offsets::ConcreteWeapon_BallisticBarrel);
            if (IsObjectOfClass(barrel, g_classes.EBBarrel))
            {
                g_diag.BallisticBarrel = barrel;
                const float rawSpeed = Memory::Read<float>(barrel +
                    Offsets::EBBarrel_MuzzleVelocity);
                if (std::isfinite(rawSpeed) && rawSpeed > 1.0f && rawSpeed < 1000000.0f)
                    g_diag.ProjectileSpeedCmPerSecond = rawSpeed < 5000.0f ?
                        rawSpeed * 100.0f : rawSpeed;
            }
        }
        g_diag.InventoryComponent = FindComponent(g_diag.Pawn, g_classes.InventoryComponent);
        if (g_diag.Pawn)
            g_diag.SenseStimulusComponent = Memory::Read<uintptr_t>(g_diag.Pawn +
                Offsets::IRRBaseCharacter_SenseStimulusComponent);
        g_diag.SenseStimulusComponentTypeValid = IsObjectOfClass(
            g_diag.SenseStimulusComponent, g_classes.SenseStimulusComponent);
        g_diag.StaminaArmObject = FindOwnedObject(g_staminaArmObjects, g_diag.Pawn);
        g_diag.WeaponRecoilObject = FindOwnedObject(g_weaponRecoilObjects, g_diag.Pawn);
        if (g_diag.Pawn)
        {
            for (const uintptr_t object : g_staminaObjects)
            {
                if (!ObjectOwnedBy(object, g_diag.Pawn))
                    continue;
                const uintptr_t attribute = Memory::Read<uintptr_t>(object +
                    Offsets::FirstPersonStamina_StaminaAttribute);
                if (IsObjectOfClass(attribute, g_classes.SimpleGameplayAttribute))
                {
                    if (std::find(g_staminaAttributes.begin(), g_staminaAttributes.end(),
                            attribute) == g_staminaAttributes.end())
                        g_staminaAttributes.push_back(attribute);
                    if (!g_diag.StaminaObject)
                    {
                        g_diag.StaminaObject = object;
                        g_diag.StaminaAttribute = attribute;
                    }
                }
            }
        }
        g_diag.StaminaAttributeCount = static_cast<int32_t>(g_staminaAttributes.size());

        if (!g_diag.ObjectArrayValid)
            g_diag.FailureStage = "GUObjectArray configured RVA + section scan";
        else if (!g_diag.World && !g_diag.GWorldSlot)
            g_diag.FailureStage = "GWorld RVA slot and active-world fallbacks";
        else if (!g_diag.World && !g_diag.RawGWorld)
            g_diag.FailureStage = "GWorld null and no validated active fallback";
        else if (!g_diag.World) g_diag.FailureStage = "no validated active raid world";
        else if (!g_diag.PersistentLevel) g_diag.FailureStage = "World::PersistentLevel";
        else if (!g_diag.GameInstance) g_diag.FailureStage = "live GeneralGameInstance";
        else if (!g_diag.LocalPlayer) g_diag.FailureStage = "local player (direct + fallback)";
        else if (!g_diag.PlayerController) g_diag.FailureStage = "local PlayerController";
        else if (!g_diag.Pawn) g_diag.FailureStage = "Controller::Pawn/AcknowledgedPawn";
        else if (!g_diag.CameraManager) g_diag.FailureStage = "PlayerCameraManager";
        else g_diag.FailureStage = "ready";

        LogRuntimeStateIfNeeded();
    }

    const RuntimeDiagnostics& GetDiagnostics() { return g_diag; }
    PoseCacheDiagnostics GetPoseCacheDiagnostics()
    {
        PoseCacheDiagnostics out{};
        {
            std::lock_guard<std::mutex> lock(g_poseCacheMutex);
            out.CachedActors = static_cast<int32_t>(g_poseCache.size());
        }
        out.LastRequestedActors = g_poseLastRequestedActors.load();
        out.LastSampledActors = g_poseLastSampledActors.load();
        out.LastAggregateActors = g_poseLastAggregateActors.load();
        out.LastFallbackActors = g_poseLastFallbackActors.load();
        out.LastCompletedAt = g_poseLastCompletedAt.load();
        out.LastSampleThreadId = g_poseLastSampleThreadId.load();
        out.TaskPending = g_poseTaskPending.load();
        return out;
    }
    VisibilityDiagnostics GetVisibilityDiagnostics()
    {
        VisibilityDiagnostics out{};
        {
            std::lock_guard<std::mutex> lock(g_visibilityCacheMutex);
            out.CachedActors = static_cast<int32_t>(g_visibilityCache.size());
            out.QueuedActors = static_cast<int32_t>(g_visibilityQueue.size());
            for (const auto& item : g_visibilityCache)
                if (item.second.Visible)
                    ++out.VisibleActors;
        }
        out.LastRequestedActors = g_visibilityLastRequestedActors.load();
        out.LastVisibleActors = g_visibilityLastVisibleActors.load();
        out.LastCompletedAt = g_visibilityLastCompletedAt.load();
        out.LastSampleThreadId = g_visibilityLastSampleThreadId.load();
        out.TaskPending = g_visibilityTaskPending.load();
        return out;
    }
    uintptr_t GetWorld() { return g_diag.World; }
    uintptr_t GetPersistentLevel() { return g_diag.PersistentLevel; }
    uintptr_t GetGameInstance() { return g_diag.GameInstance; }
    uintptr_t GetLocalPlayer() { return g_diag.LocalPlayer; }
    uintptr_t GetLocalController() { return g_diag.PlayerController; }
    uintptr_t GetLocalPawn() { return g_diag.Pawn; }
    uintptr_t GetCameraManager() { return g_diag.CameraManager; }
    uintptr_t GetEquippedWeapon() { return g_diag.EquippedWeapon; }
    uintptr_t GetWeaponComponent() { return g_diag.WeaponComponent; }
    uintptr_t GetBallisticBarrel() { return g_diag.BallisticBarrel; }
    uintptr_t GetInventoryComponent() { return g_diag.InventoryComponent; }
    uintptr_t GetStaminaObject() { return g_diag.StaminaObject; }
    uintptr_t GetStaminaAttribute() { return g_diag.StaminaAttribute; }
    uintptr_t GetStaminaArmObject() { return g_diag.StaminaArmObject; }
    uintptr_t GetWeaponRecoilObject() { return g_diag.WeaponRecoilObject; }
    const std::vector<uintptr_t>& GetStaminaAttributes() { return g_staminaAttributes; }
    const std::vector<uintptr_t>& GetActors() { return g_actors; }
    const std::vector<uintptr_t>& GetCharacters() { return g_characters; }

    bool IsInstanceOf(uintptr_t object, uintptr_t targetClass)
    {
        return IsObjectOfClass(object, targetClass);
    }

    bool IsIRRCharacter(uintptr_t actor)
    {
        return IsObjectOfClass(actor, g_classes.IRRBaseCharacter);
    }

    bool IsEnemyCharacter(uintptr_t actor)
    {
        if (!actor || actor == GetLocalPawn() || !IsIRRCharacter(actor))
            return false;
        if (g_hostileCharacters.find(actor) != g_hostileCharacters.end())
            return true;
        // AI characters are the only non-local combatants in a solo raid. This
        // fallback also covers the short interval before TeamComponent::Hostiles
        // has replicated, which previously left zero aimbot candidates.
        if (g_classes.IRRAIBaseCharacter &&
            IsObjectOfClass(actor, g_classes.IRRAIBaseCharacter))
            return true;
        // Do not broaden an unavailable team list to every IRRBaseCharacter: that
        // made stale/neutral actors appear as enemies. In a raid, the validated AI
        // class is the safe fallback until Hostiles replicates.
        return false;
    }

    bool InvokeFunctionRaw(uintptr_t object, int32_t functionIndex,
                           void* params, size_t paramSize)
    {
        g_lastFunctionIndex = functionIndex;
        g_lastFunctionObject = 0;
        g_processEventAddress = 0;
        g_processEventValid = false;
        g_lastProcessEventCallSucceeded = false;

        if (!object || (paramSize && !params) || !g_objects.Valid || !g_classes.Function ||
            functionIndex < 0 || functionIndex >= g_objects.Count)
            return false;
        const uintptr_t function = GetObjectAt(g_objects, functionIndex);
        g_lastFunctionObject = function;
        if (!function || !IsObjectOfClass(function, g_classes.Function) ||
            Memory::Read<int32_t>(function + Offsets::UObject_InternalIndex) != functionIndex)
            return false;

        const uintptr_t vtable = Memory::Read<uintptr_t>(object);
        // Dumper-7's older UE 5.3 sample used 0x4D, but the live Test_C UE 5.6.1
        // vtable and executable disassembly prove ProcessEvent is 0x4C. Slot 0x4D
        // is only `mov eax, 2; ret` and was the common ammo/invisibility failure.
        constexpr size_t ProcessEventVtableIndex = 0x4C;
        const uintptr_t processEvent = vtable ? Memory::Read<uintptr_t>(vtable +
            ProcessEventVtableIndex * sizeof(uintptr_t)) : 0;
        g_processEventAddress = processEvent;
        const uintptr_t moduleBase = Memory::GetBase();
        const uintptr_t moduleEnd = moduleBase + Memory::GetModuleSize();
        std::array<uint8_t, 4> prologue{};
        const bool prologueMatches = Memory::ReadRaw(processEvent, prologue.data(),
            prologue.size()) && prologue[0] == 0x40 && prologue[1] == 0x55 &&
            prologue[2] == 0x56 && prologue[3] == 0x57;
        g_processEventValid = processEvent >= moduleBase && processEvent < moduleEnd &&
            Memory::IsExecutable(processEvent) && prologueMatches;
        if (!g_processEventValid)
            return false;

        using ProcessEventFn = void(__fastcall*)(void*, void*, void*);
#ifdef _MSC_VER
        __try
        {
            reinterpret_cast<ProcessEventFn>(processEvent)(
                reinterpret_cast<void*>(object), reinterpret_cast<void*>(function), params);
            g_lastProcessEventCallSucceeded = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_lastProcessEventCallSucceeded = false;
        }
#else
        reinterpret_cast<ProcessEventFn>(processEvent)(
            reinterpret_cast<void*>(object), reinterpret_cast<void*>(function), params);
        g_lastProcessEventCallSucceeded = true;
#endif
        g_diag.ProcessEventAddress = g_processEventAddress;
        g_diag.LastFunctionObject = g_lastFunctionObject;
        g_diag.LastFunctionIndex = g_lastFunctionIndex;
        g_diag.ProcessEventValid = g_processEventValid;
        g_diag.LastProcessEventCallSucceeded = g_lastProcessEventCallSucceeded;
        return g_lastProcessEventCallSucceeded;
    }

    bool InvokeBooleanFunction(uintptr_t object, int32_t functionIndex, bool value)
    {
        alignas(8) uint8_t params[8]{};
        params[0] = value ? 1u : 0u;
        return InvokeFunctionRaw(object, functionIndex, params, sizeof(params));
    }

    bool QueryBooleanFunction(uintptr_t object, int32_t functionIndex, bool& outValue)
    {
        alignas(8) uint8_t params[8]{};
        if (!InvokeFunctionRaw(object, functionIndex, params, sizeof(params)))
            return false;
        outValue = params[0] != 0;
        return true;
    }

    bool QueryByteFunction(uintptr_t object, int32_t functionIndex, uint8_t& outValue)
    {
        alignas(8) uint8_t params[8]{};
        if (!InvokeFunctionRaw(object, functionIndex, params, sizeof(params)))
            return false;
        outValue = params[0];
        return true;
    }

    bool QueryFloatFunction(uintptr_t object, int32_t functionIndex, float& outValue)
    {
        alignas(8) uint8_t params[8]{};
        if (!InvokeFunctionRaw(object, functionIndex, params, sizeof(params)))
            return false;
        float value = 0.0f;
        std::memcpy(&value, params, sizeof(value));
        if (!std::isfinite(value))
            return false;
        outValue = value;
        return true;
    }

    uintptr_t GetObjectByIndex(int32_t objectIndex)
    {
        if (!g_objects.Valid || objectIndex < 0 || objectIndex >= g_objects.Count)
            return 0;
        return GetObjectAt(g_objects, objectIndex);
    }

    bool GetObjectNameToken(int32_t objectIndex, FName& outName)
    {
        outName = {};
        if (!g_objects.Valid || objectIndex < 0 || objectIndex >= g_objects.Count)
            return false;
        const uintptr_t object = GetObjectAt(g_objects, objectIndex);
        if (!object || Memory::Read<int32_t>(object + Offsets::UObject_InternalIndex) != objectIndex)
            return false;
        return Memory::TryRead(object + Offsets::UObject_NamePrivate, outName) &&
               outName.IsValid();
    }

    bool GetActorEyesViewPoint(uintptr_t actor, FVector& outLocation)
    {
        outLocation = {};
        if (!actor)
            return false;
        const DWORD windowThread = GetGameWindowThreadId();
        if (windowThread && GetCurrentThreadId() != windowThread)
            return GetPoseAwareBodyTarget(actor, "head", outLocation);
        alignas(8) uint8_t params[0x30]{};
        if (!InvokeFunctionRaw(actor, FunctionIndices::Actor_GetActorEyesViewPoint,
                               params, sizeof(params)))
            return false;
        std::memcpy(&outLocation, params + 0x00, sizeof(outLocation));
        return outLocation.IsFinite();
    }

    bool GetPoseAwareBodyTarget(uintptr_t actor, const std::string& targetName,
                                FVector& outLocation, uintptr_t* outBodyComponent)
    {
        outLocation = {};
        if (outBodyComponent)
            *outBodyComponent = 0;
        if (!actor || !IsIRRCharacter(actor))
            return false;

        const DWORD windowThread = GetGameWindowThreadId();
        if (windowThread && GetCurrentThreadId() != windowThread)
        {
            PoseCacheEntry cached{};
            {
                std::lock_guard<std::mutex> lock(g_poseCacheMutex);
                const auto found = g_poseCache.find(actor);
                if (found == g_poseCache.end() || !found->second.SampledAt ||
                    GetTickCount64() - found->second.SampledAt > 500)
                    return false;
                cached = found->second;
            }
            if (Memory::Read<uintptr_t>(actor +
                    Offsets::IRRBaseCharacter_BodyComponent) != cached.BodyComponent)
                return false;
            FVector currentLocation{};
            if (!GetActorLocation(actor, currentLocation))
                return false;
            const FVector delta = currentLocation - cached.ActorLocation;
            if (outBodyComponent)
                *outBodyComponent = cached.BodyComponent;
            return PoseTargetFromEntry(cached, targetName, delta, outLocation);
        }

        const uintptr_t body = Memory::Read<uintptr_t>(actor +
            Offsets::IRRBaseCharacter_BodyComponent);
        if (!body || !IsObjectOfClass(body, g_classes.IRRBodyComponent))
            return false;
        if (outBodyComponent)
            *outBodyComponent = body;

        FVector actorLocation{};
        if (!GetActorLocation(actor, actorLocation))
            return false;
        auto plausible = [&](const FVector& point)
        {
            return point.IsFinite() && point.Distance(actorLocation) < 500.0;
        };
        auto getEye = [&](FVector& point)
        {
            alignas(8) uint8_t params[0x18]{};
            if (!InvokeFunctionRaw(body,
                    FunctionIndices::IRRBodyComponent_GetEyeLocation,
                    params, sizeof(params)))
                return false;
            std::memcpy(&point, params, sizeof(point));
            return plausible(point);
        };
        auto getPart = [&](uint8_t part, FVector& point)
        {
            alignas(8) uint8_t params[0x20]{};
            params[0] = part;
            if (!InvokeFunctionRaw(body,
                    FunctionIndices::IRRBodyComponent_GetBodyPartLocation,
                    params, sizeof(params)))
                return false;
            std::memcpy(&point, params + 0x08, sizeof(point));
            return plausible(point);
        };

        // EIRRBodyPart values were recovered from the updated shipping image in
        // declaration order: Head, Thorax, Stomach, RightArm, LeftArm,
        // RightLeg, LeftLeg, RightFoot, LeftFoot (0..8).
        constexpr uint8_t Head = 0;
        constexpr uint8_t Thorax = 1;
        constexpr uint8_t Stomach = 2;
        constexpr uint8_t RightArm = 3;
        constexpr uint8_t LeftArm = 4;
        constexpr uint8_t RightLeg = 5;
        constexpr uint8_t LeftLeg = 6;
        constexpr uint8_t RightFoot = 7;
        constexpr uint8_t LeftFoot = 8;

        if (targetName == "head")
            return getEye(outLocation) || getPart(Head, outLocation);
        if (targetName == "neck")
        {
            FVector eye{};
            FVector thorax{};
            if (getEye(eye) && getPart(Thorax, thorax))
            {
                // The body enum has no neck entry. Interpolating between two live,
                // pose-aware anchors follows crouch/prone animation correctly.
                outLocation = thorax + (eye - thorax) * 0.68;
                return plausible(outLocation);
            }
            return getPart(Head, outLocation);
        }
        if (targetName == "chest")
            return getPart(Thorax, outLocation);
        if (targetName == "stomach" || targetName == "pelvis")
            return getPart(Stomach, outLocation);
        if (targetName == "arm_r" || targetName == "hand_r")
            return getPart(RightArm, outLocation);
        if (targetName == "arm_l" || targetName == "hand_l")
            return getPart(LeftArm, outLocation);
        if (targetName == "leg_r")
            return getPart(RightLeg, outLocation);
        if (targetName == "leg_l")
            return getPart(LeftLeg, outLocation);
        if (targetName == "foot_r")
            return getPart(RightFoot, outLocation);
        if (targetName == "foot_l")
            return getPart(LeftFoot, outLocation);
        return false;
    }

    bool RequestPoseSamples(const std::vector<uintptr_t>& actors,
                            uint32_t minimumIntervalMs)
    {
        if (actors.empty() || !GetGameWindowThreadId())
            return false;

        const ULONGLONG now = GetTickCount64();
        const uintptr_t world = GetWorld();
        {
            std::lock_guard<std::mutex> lock(g_poseCacheMutex);
            if (g_poseCacheWorld != world)
            {
                g_poseCache.clear();
                g_poseQueuedActors.clear();
                g_poseCacheWorld = world;
            }
            for (const uintptr_t actor : actors)
            {
                if (actor && g_poseQueuedActors.size() < 32)
                    g_poseQueuedActors.insert(actor);
            }
        }
        if (g_poseTaskPending.load())
            return true;
        if (g_lastPoseRequestAt && now - g_lastPoseRequestAt < minimumIntervalMs)
            return true;

        std::vector<uintptr_t> requested;
        {
            std::lock_guard<std::mutex> lock(g_poseCacheMutex);
            requested.reserve(std::min<size_t>(g_poseQueuedActors.size(), 16));
            for (auto it = g_poseQueuedActors.begin();
                 it != g_poseQueuedActors.end() && requested.size() < 16;)
            {
                requested.push_back(*it);
                it = g_poseQueuedActors.erase(it);
            }
        }
        if (requested.empty())
            return false;

        g_lastPoseRequestAt = now;
        g_poseLastRequestedActors.store(static_cast<int32_t>(requested.size()));
        g_poseTaskPending.store(true);
        if (!QueueGameThreadTask([requested = std::move(requested), world]()
            {
                std::vector<std::pair<uintptr_t, PoseCacheEntry>> completed;
                completed.reserve(requested.size());
                int32_t expensiveSamples = 0;
                for (const uintptr_t actor : requested)
                {
                    PoseCacheEntry previous{};
                    bool hasPrevious = false;
                    {
                        std::lock_guard<std::mutex> lock(g_poseCacheMutex);
                        const auto found = g_poseCache.find(actor);
                        if (found != g_poseCache.end())
                        {
                            previous = found->second;
                            hasPrevious = true;
                        }
                    }
                    if (hasPrevious && previous.UsedIndividualFallback &&
                        previous.SampledAt &&
                        GetTickCount64() - previous.SampledAt < 250)
                    {
                        std::lock_guard<std::mutex> lock(g_poseCacheMutex);
                        if (g_poseCacheWorld == world)
                            g_poseQueuedActors.insert(actor);
                        continue;
                    }
                    const bool mayNeedIndividualCalls = !hasPrevious ||
                        previous.UsedIndividualFallback;
                    if (mayNeedIndividualCalls && expensiveSamples >= 2)
                    {
                        std::lock_guard<std::mutex> lock(g_poseCacheMutex);
                        if (g_poseCacheWorld == world)
                            g_poseQueuedActors.insert(actor);
                        continue;
                    }
                    if (mayNeedIndividualCalls)
                        ++expensiveSamples;
                    PoseCacheEntry sample{};
                    if (ReadBodyPoseDirect(actor,
                            hasPrevious ? &previous : nullptr, sample))
                        completed.emplace_back(actor, sample);
                }

                const ULONGLONG completedAt = GetTickCount64();
                int32_t fallbackCount = 0;
                for (const auto& item : completed)
                    if (item.second.UsedIndividualFallback)
                        ++fallbackCount;
                {
                    std::lock_guard<std::mutex> lock(g_poseCacheMutex);
                    if (g_poseCacheWorld == world)
                    {
                        for (const auto& item : completed)
                            g_poseCache[item.first] = item.second;
                        for (auto it = g_poseCache.begin(); it != g_poseCache.end();)
                        {
                            if (!it->second.SampledAt ||
                                completedAt - it->second.SampledAt > 2000)
                                it = g_poseCache.erase(it);
                            else
                                ++it;
                        }
                    }
                }
                g_poseLastSampledActors.store(static_cast<int32_t>(completed.size()));
                g_poseLastFallbackActors.store(fallbackCount);
                g_poseLastAggregateActors.store(
                    static_cast<int32_t>(completed.size()) - fallbackCount);
                g_poseLastSampleThreadId.store(GetCurrentThreadId());
                g_poseLastCompletedAt.store(completedAt);
                g_poseTaskPending.store(false);
            }, false))
        {
            g_poseTaskPending.store(false);
            return false;
        }
        return true;
    }

    bool RequestVisibilitySamples(const std::vector<uintptr_t>& actors,
                                  const std::string& preferredTarget,
                                  uint32_t minimumIntervalMs)
    {
        const uintptr_t world = GetWorld();
        const uintptr_t library = GetObjectByIndex(
            ObjectIndices::DefaultGeneralFunctionLibrary);
        const uintptr_t controller = GetLocalController();
        const Camera camera = GetRenderCamera();
        if (actors.empty() || !world || !library || !camera.Valid ||
            !GetGameWindowThreadId())
            return false;

        // Warm the pose cache independently. Visibility work consumes immutable
        // points only; all reflected ray tests are posted to the game thread below.
        RequestPoseSamples(actors, std::max<uint32_t>(minimumIntervalMs, 75));

        static constexpr std::array<const char*, 10> BodyTargets = {
            "head", "neck", "chest", "stomach", "arm_l", "arm_r",
            "leg_l", "leg_r", "foot_l", "foot_r"
        };

        std::vector<VisibilityWork> prepared;
        prepared.reserve(std::min<size_t>(actors.size(), 32));
        for (const uintptr_t actor : actors)
        {
            if (!actor || prepared.size() >= 128 || !IsEnemyCharacter(actor))
                continue;

            VisibilityWork work{};
            work.Actor = actor;
            work.ObserverLocation = camera.Location;
            if (!GetActorLocation(actor, work.ActorLocation))
                continue;
            work.BroadRadius = std::clamp(GetCapsuleHalfHeight(actor) * 0.90f,
                                          45.0f, 95.0f);

            auto addPoint = [&](const FVector& point)
            {
                if (!point.IsFinite() || work.PointCount >= work.Points.size() ||
                    point.Distance(work.ActorLocation) > 300.0)
                    return;
                for (uint8_t index = 0; index < work.PointCount; ++index)
                    if (work.Points[index].Distance(point) < 2.0)
                        return;
                work.Points[work.PointCount++] = point;
            };
            auto addTarget = [&](const std::string& name)
            {
                FVector point{};
                if (GetPoseAwareBodyTarget(actor, name, point))
                    addPoint(point);
            };

            // The selected bone is tested first. If it is covered, the remaining
            // live anchors allow an exposed head, torso, arm, leg, or foot to win.
            addTarget(preferredTarget);
            for (const char* target : BodyTargets)
                if (preferredTarget != target)
                    addTarget(target);

            if (!work.PointCount)
            {
                const double halfHeight = static_cast<double>(
                    GetCapsuleHalfHeight(actor));
                for (const double factor : { 0.78, 0.58, 0.32, -0.10, -0.48, -0.78 })
                    addPoint(work.ActorLocation + FVector(0.0, 0.0,
                        halfHeight * factor));
            }
            if (work.PointCount)
                prepared.push_back(work);
        }
        if (prepared.empty())
            return false;

        {
            std::lock_guard<std::mutex> lock(g_visibilityCacheMutex);
            if (g_visibilityCacheWorld != world)
            {
                g_visibilityCache.clear();
                g_visibilityQueue.clear();
                g_visibilityCacheWorld = world;
            }
            for (const VisibilityWork& work : prepared)
            {
                const auto queued = std::find_if(g_visibilityQueue.begin(),
                    g_visibilityQueue.end(), [&](const VisibilityWork& item)
                    { return item.Actor == work.Actor; });
                if (queued != g_visibilityQueue.end())
                    *queued = work;
                else if (g_visibilityQueue.size() < 128)
                    g_visibilityQueue.push_back(work);
            }
        }

        const ULONGLONG now = GetTickCount64();
        if (g_visibilityTaskPending.load() ||
            (g_lastVisibilityRequestAt &&
             now - g_lastVisibilityRequestAt < minimumIntervalMs))
            return true;

        std::vector<VisibilityWork> requested;
        {
            std::lock_guard<std::mutex> lock(g_visibilityCacheMutex);
            const size_t count = std::min<size_t>(g_visibilityQueue.size(), 16);
            requested.assign(g_visibilityQueue.begin(),
                             g_visibilityQueue.begin() + count);
            g_visibilityQueue.erase(g_visibilityQueue.begin(),
                                    g_visibilityQueue.begin() + count);
        }
        if (requested.empty())
            return false;

        g_lastVisibilityRequestAt = now;
        g_visibilityLastRequestedActors.store(
            static_cast<int32_t>(requested.size()));
        g_visibilityTaskPending.store(true);
        if (!QueueGameThreadTask([requested = std::move(requested), world,
                                  library, controller]()
            {
                // Controller::LineOfSightTo is the engine's authoritative trace and
                // remains reliable when the Blueprint sphere helper has not warmed
                // its camera/context state yet.  Keep the Blueprint sampler as the
                // multi-point fallback so a visible limb can still unlock a target.
                auto controllerViewPoint = [&](FVector& outLocation)
                {
                    if (!controller)
                        return false;
                    alignas(16) std::array<uint8_t, 0x30> params{};
                    if (!InvokeFunctionRaw(controller,
                            FunctionIndices::Controller_GetPlayerViewPoint,
                            params.data(), params.size()))
                        return false;
                    std::memcpy(&outLocation, params.data(), sizeof(outLocation));
                    return outLocation.IsFinite();
                };

                FVector engineViewPoint{};
                const bool haveEngineViewPoint = controllerViewPoint(engineViewPoint);

                auto controllerLineOfSight = [&](uintptr_t actor,
                                                  const FVector& viewpoint)
                {
                    if (!controller || !actor)
                        return false;
                    auto invoke = [&](const FVector& point)
                    {
                        alignas(16) std::array<uint8_t, 0x28> params{};
                        std::memcpy(params.data() + 0x00, &actor, sizeof(actor));
                        std::memcpy(params.data() + 0x08, &point, sizeof(point));
                        params[0x20] = 0; // bAlternateChecks
                        return InvokeFunctionRaw(controller,
                            FunctionIndices::Controller_LineOfSightTo,
                            params.data(), params.size()) && params[0x21] != 0;
                    };

                    // UE's documented/native convention is a zero ViewPoint:
                    // LineOfSightTo then asks the controller for its current eye
                    // location. Passing a cached render-camera position here can
                    // be one frame stale and made every open target look hidden.
                    if (invoke(FVector{}))
                        return true;
                    // Keep the current engine viewpoint as a secondary probe for
                    // custom controller implementations that do not honor zero.
                    return viewpoint.IsFinite() && viewpoint.Length() > 1.0 &&
                        invoke(viewpoint);
                };

                auto checkSphere = [&](const FVector& observer,
                                       const FVector& center,
                                       float radius, int32_t rays)
                {
                    alignas(16) std::array<uint8_t, 0x48> params{};
                    std::memcpy(params.data() + 0x00, &world, sizeof(world));
                    std::memcpy(params.data() + 0x08, &observer, sizeof(observer));
                    std::memcpy(params.data() + 0x20, &center, sizeof(center));
                    std::memcpy(params.data() + 0x38, &radius, sizeof(radius));
                    std::memcpy(params.data() + 0x3C, &rays, sizeof(rays));
                    return InvokeFunctionRaw(library,
                        FunctionIndices::GeneralFunctionLibrary_CheckSphereVisibility,
                        params.data(), params.size()) && params[0x40] != 0;
                };

                std::vector<std::pair<uintptr_t, VisibilityCacheEntry>> completed;
                completed.reserve(requested.size());
                int32_t visibleCount = 0;
                for (const VisibilityWork& work : requested)
                {
                    VisibilityCacheEntry result{};
                    result.ActorLocation = work.ActorLocation;
                    result.SampledAt = GetTickCount64();
                    const FVector observer = haveEngineViewPoint ?
                        engineViewPoint : work.ObserverLocation;

                    // Prefer the native actor trace. It tests the actual actor and
                    // avoids the false-negative window while CheckSphereVisibility
                    // is still waiting for its Blueprint camera context. Once the
                    // actor is known visible, retain the selected body point and use
                    // sphere samples only to find a more exposed limb/anchor.
                    const bool actorVisible = GetWorld() == world &&
                        controllerLineOfSight(work.Actor, observer);
                    const bool broadVisible = !actorVisible && GetWorld() == world &&
                        checkSphere(observer, work.ActorLocation,
                                    work.BroadRadius, 9);
                    if (actorVisible || broadVisible)
                    {
                        for (uint8_t index = 0; index < work.PointCount; ++index)
                        {
                            if (!actorVisible && !checkSphere(observer,
                                             work.Points[index], 14.0f, 3))
                                continue;
                            result.Visible = true;
                            result.ExposedPoint = work.Points[index];
                            ++visibleCount;
                            break;
                        }
                        // A native actor trace can succeed even when an individual
                        // tiny sphere sample is rejected at an animation seam. Keep
                        // the requested point in that case instead of reporting a
                        // fully hidden actor and disabling target acquisition.
                        if (actorVisible && !result.Visible && work.PointCount)
                        {
                            result.Visible = true;
                            result.ExposedPoint = work.Points[0];
                            ++visibleCount;
                        }
                    }
                    completed.emplace_back(work.Actor, result);
                }

                const ULONGLONG completedAt = GetTickCount64();
                {
                    std::lock_guard<std::mutex> lock(g_visibilityCacheMutex);
                    if (g_visibilityCacheWorld == world)
                    {
                        for (const auto& item : completed)
                            g_visibilityCache[item.first] = item.second;
                        for (auto it = g_visibilityCache.begin();
                             it != g_visibilityCache.end();)
                        {
                            if (!it->second.SampledAt ||
                                completedAt - it->second.SampledAt > 2500)
                                it = g_visibilityCache.erase(it);
                            else
                                ++it;
                        }
                    }
                }
                g_visibilityLastVisibleActors.store(visibleCount);
                g_visibilityLastSampleThreadId.store(GetCurrentThreadId());
                g_visibilityLastCompletedAt.store(completedAt);
                g_visibilityTaskPending.store(false);
            }, false))
        {
            g_visibilityTaskPending.store(false);
            return false;
        }
        return true;
    }

    bool GetCachedVisibility(uintptr_t actor, bool& outVisible,
                             FVector* outExposedPoint,
                             uint32_t maximumAgeMs)
    {
        outVisible = false;
        if (outExposedPoint)
            *outExposedPoint = {};
        if (!actor)
            return false;

        VisibilityCacheEntry cached{};
        {
            std::lock_guard<std::mutex> lock(g_visibilityCacheMutex);
            if (g_visibilityCacheWorld != GetWorld())
                return false;
            const auto found = g_visibilityCache.find(actor);
            if (found == g_visibilityCache.end() || !found->second.SampledAt ||
                GetTickCount64() - found->second.SampledAt > maximumAgeMs)
                return false;
            cached = found->second;
        }

        outVisible = cached.Visible;
        if (outExposedPoint && cached.Visible)
        {
            FVector currentLocation{};
            const FVector delta = GetActorLocation(actor, currentLocation) ?
                currentLocation - cached.ActorLocation : FVector{};
            *outExposedPoint = cached.ExposedPoint + delta;
        }
        return true;
    }

    bool GetActorVelocity(uintptr_t actor, FVector& outVelocity)
    {
        outVelocity = {};
        if (!actor)
            return false;
        const uintptr_t movement = Memory::Read<uintptr_t>(actor +
            Offsets::ACharacter_CharacterMovement);
        if (!movement || !Memory::TryRead(movement + Offsets::MovementComponent_Velocity,
                                          outVelocity) || !outVelocity.IsFinite() ||
            outVelocity.Length() > 20000.0)
        {
            outVelocity = {};
            return false;
        }
        return true;
    }

    float GetProjectileSpeedCmPerSecond()
    {
        return g_diag.ProjectileSpeedCmPerSecond;
    }

    bool PredictBallisticAim(const FVector& start, const FVector& target,
                             const FVector& targetVelocity, float maxTime,
                             FVector& outAimPoint, float& outFlightTime,
                             bool& outUsedGameSolver)
    {
        outAimPoint = target;
        outFlightTime = 0.0f;
        outUsedGameSolver = false;
        if (!start.IsFinite() || !target.IsFinite() || !targetVelocity.IsFinite())
            return false;
        maxTime = std::clamp(maxTime, 0.05f, 5.0f);

        const uintptr_t barrel = GetBallisticBarrel();
        const uintptr_t bulletClass = barrel ? Memory::Read<uintptr_t>(barrel +
            Offsets::EBBarrel_ChamberedBullet) : 0;
        const DWORD windowThread = GetGameWindowThreadId();
        if (barrel && bulletClass &&
            (!windowThread || GetCurrentThreadId() == windowThread))
        {
            struct alignas(8) Params
            {
                uintptr_t BulletClass = 0;             // 0x00
                FVector StartLocation{};               // 0x08
                FVector TargetLocation{};              // 0x20
                FVector TargetVelocity{};              // 0x38
                FVector AimDirection{};                // 0x50
                FVector PredictedTargetLocation{};     // 0x68
                FVector PredictedIntersection{};       // 0x80
                float PredictedFlightTime = 0.0f;      // 0x98
                float Error = 1.0f;                    // 0x9C
                float MaxTime = 3.0f;                  // 0xA0
                float Step = 0.015f;                   // 0xA4
                int32_t NumIterations = 96;            // 0xA8
                int32_t Padding = 0;
            } params{};
            static_assert(offsetof(Params, StartLocation) == 0x08);
            static_assert(offsetof(Params, AimDirection) == 0x50);
            static_assert(offsetof(Params, PredictedFlightTime) == 0x98);
            params.BulletClass = bulletClass;
            params.StartLocation = start;
            params.TargetLocation = target;
            params.TargetVelocity = targetVelocity;
            params.MaxTime = maxTime;

            if (InvokeFunctionRaw(barrel,
                    FunctionIndices::EBBarrel_CalculateAimDirectionFromLocation,
                    &params, sizeof(params)))
            {
                const double directionLength = params.AimDirection.Length();
                if (params.AimDirection.IsFinite() && directionLength > 0.5 &&
                    directionLength < 1.5 && std::isfinite(params.PredictedFlightTime) &&
                    params.PredictedFlightTime > 0.0f &&
                    params.PredictedFlightTime <= maxTime * 1.25f)
                {
                    const double rayLength = std::max(target.Distance(start), 10000.0);
                    outAimPoint = start + params.AimDirection *
                        (rayLength / directionLength);
                    outFlightTime = params.PredictedFlightTime;
                    outUsedGameSolver = outAimPoint.IsFinite();
                    if (outUsedGameSolver)
                        return true;
                }
            }
        }

        // Fallback for an empty chamber or unavailable barrel component: solve the
        // constant-velocity interception equation with the live muzzle velocity,
        // then compensate ordinary UE gravity. This remains bone/limb-relative.
        const double speed = static_cast<double>(GetProjectileSpeedCmPerSecond());
        if (!(speed > 1000.0 && speed < 1000000.0))
            return false;
        const FVector relative = target - start;
        const double a = targetVelocity.Dot(targetVelocity) - speed * speed;
        const double b = 2.0 * relative.Dot(targetVelocity);
        const double c = relative.Dot(relative);
        double time = 0.0;
        if (std::abs(a) < 0.000001)
        {
            if (std::abs(b) > 0.000001)
                time = -c / b;
        }
        else
        {
            const double discriminant = b * b - 4.0 * a * c;
            if (discriminant >= 0.0)
            {
                const double root = std::sqrt(discriminant);
                const double first = (-b - root) / (2.0 * a);
                const double second = (-b + root) / (2.0 * a);
                if (first > 0.0 && second > 0.0) time = std::min(first, second);
                else time = std::max(first, second);
            }
        }
        if (!(time > 0.0 && time <= maxTime))
            return false;
        outAimPoint = target + targetVelocity * time;
        outAimPoint.Z += 0.5 * 980.0 * time * time;
        outFlightTime = static_cast<float>(time);
        return outAimPoint.IsFinite();
    }

    bool ProjectWorldToScreen(const FVector& world, Vector2& out)
    {
        const uintptr_t controller = GetLocalController();
        if (!controller || !world.IsFinite())
            return false;
        alignas(8) uint8_t params[0x30]{};
        std::memcpy(params + 0x00, &world, sizeof(world));
        // bPlayerViewportRelative @ 0x28 remains false. ReturnValue is @ 0x29.
        if (!InvokeFunctionRaw(controller,
                FunctionIndices::PlayerController_ProjectWorldLocationToScreen,
                params, sizeof(params)) || params[0x29] == 0)
            return false;
        double x = 0.0;
        double y = 0.0;
        std::memcpy(&x, params + 0x18, sizeof(x));
        std::memcpy(&y, params + 0x20, sizeof(y));
        if (!std::isfinite(x) || !std::isfinite(y))
            return false;
        out.x = static_cast<float>(x);
        out.y = static_cast<float>(y);
        return true;
    }

    Camera GetCamera()
    {
        Camera out{};
        const uintptr_t manager = GetCameraManager();
        if (!manager)
            return out;
        const uintptr_t pov = manager + Offsets::PlayerCameraManager_CameraCachePrivate +
                              Offsets::CameraCacheEntry_POV;
        if (!Memory::TryRead(pov + Offsets::MinimalViewInfo_Location, out.Location) ||
            !Memory::TryRead(pov + Offsets::MinimalViewInfo_Rotation, out.Rotation) ||
            !Memory::TryRead(pov + Offsets::MinimalViewInfo_FOV, out.FOV))
            return out;
        out.Valid = out.Location.IsFinite() && out.Rotation.IsFinite() &&
                    out.FOV > 1.0f && out.FOV < 179.0f;
        return out;
    }

    Camera GetRenderCamera()
    {
        Camera out{};
        const uintptr_t manager = GetCameraManager();
        if (!manager)
            return out;
        const uintptr_t pov = manager +
            Offsets::PlayerCameraManager_LastFrameCameraCachePrivate +
            Offsets::CameraCacheEntry_POV;
        if (Memory::TryRead(pov + Offsets::MinimalViewInfo_Location, out.Location) &&
            Memory::TryRead(pov + Offsets::MinimalViewInfo_Rotation, out.Rotation) &&
            Memory::TryRead(pov + Offsets::MinimalViewInfo_FOV, out.FOV))
        {
            out.Valid = out.Location.IsFinite() && out.Rotation.IsFinite() &&
                        out.FOV > 1.0f && out.FOV < 179.0f;
        }
        return out.Valid ? out : GetCamera();
    }

    bool GetActorLocation(uintptr_t actor, FVector& out)
    {
        if (!actor)
            return false;
        const uintptr_t root = Memory::Read<uintptr_t>(
            actor + Offsets::AActor_RootComponent);
        return root && ComponentPointToWorld(root, FVector{}, out);
    }

    float GetCapsuleHalfHeight(uintptr_t actor, float fallback)
    {
        const uintptr_t capsule = actor ? Memory::Read<uintptr_t>(actor +
            Offsets::ACharacter_CapsuleComponent) : 0;
        const float value = capsule ? Memory::Read<float>(capsule +
            Offsets::UCapsuleComponent_CapsuleHalfHeight) : 0.0f;
        return value > 10.0f && value < 300.0f ? value : fallback;
    }

    float GetCapsuleRadius(uintptr_t actor, float fallback)
    {
        const uintptr_t capsule = actor ? Memory::Read<uintptr_t>(actor +
            Offsets::ACharacter_CapsuleComponent) : 0;
        const float value = capsule ? Memory::Read<float>(capsule +
            Offsets::UCapsuleComponent_CapsuleRadius) : 0.0f;
        return value > 5.0f && value < 200.0f ? value : fallback;
    }

    Health GetHealth(uintptr_t actor)
    {
        Health out{};
        if (!IsIRRCharacter(actor))
            return out;
        out.Component = Memory::Read<uintptr_t>(actor +
            Offsets::IRRBaseCharacter_HealthComponent);
        if (!out.Component || !Memory::IsReadable(out.Component +
            Offsets::HealthComponent_Health, sizeof(uintptr_t)))
            return out;
        out.Enabled = Memory::Read<bool>(out.Component + Offsets::HealthComponent_bEnabled);
        out.Attribute = Memory::Read<uintptr_t>(out.Component + Offsets::HealthComponent_Health);
        if (!out.Attribute)
            return out;
        const uintptr_t current = out.Attribute +
            Offsets::SimpleGameplayAttribute_CurrentData;
        out.Current = Memory::Read<float>(current + Offsets::SimpleAttributeData_BaseValue);
        out.Minimum = Memory::Read<float>(current + Offsets::SimpleAttributeData_MinValue);
        out.Maximum = Memory::Read<float>(current + Offsets::SimpleAttributeData_MaxValue);
        out.Dead = Memory::Read<bool>(out.Attribute +
            Offsets::SimpleGameplayAttributeHealth_bIsDead);
        out.Valid = out.Enabled && out.Maximum > 0.0f && out.Maximum < 1000000.0f &&
                    out.Minimum > -1000000.0f && out.Minimum <= out.Maximum &&
                    out.Current > -1000000.0f && out.Current < 1000000.0f;
        return out;
    }

    bool IsLivingCharacter(uintptr_t actor)
    {
        if (!IsIRRCharacter(actor))
            return false;
        const Health health = GetHealth(actor);
        // Type identity is primary. A temporarily missing health object should not make
        // the character disappear from mesh/bone diagnostics.
        return !health.Valid || (!health.Dead && health.Current > health.Minimum);
    }

    std::wstring GetPlayerName(uintptr_t actor)
    {
        if (!actor)
            return {};
        const uintptr_t playerState = Memory::Read<uintptr_t>(actor +
            Offsets::APawn_PlayerState);
        if (!playerState)
            return {};
        const auto string = Memory::Read<TArray<wchar_t>>(playerState +
            Offsets::APlayerState_PlayerNamePrivate);
        if (!string.IsSane(512) || !string.Data || string.Count <= 0)
            return {};
        return Memory::ReadWideString(reinterpret_cast<uintptr_t>(string.Data),
            static_cast<size_t>(std::min(string.Count, 256)));
    }
}
