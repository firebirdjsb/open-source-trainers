#include "ItemMagnet.h"

#include "../Memory/Memory.h"
#include "../hooks/PresentHook.h"
#include "../sdk/GameAccess.h"
#include "../sdk/Offsets.h"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
    constexpr size_t ContainerItemSize = 0xF8;
    constexpr uintptr_t PickUpContainerItem_ContainerItem = 0x08;

    float g_rangeMeters = 35.0f;
    float g_dropDistanceMeters = 1.8f;
    float g_dropHeightMeters = 0.20f;
    int g_maxPerPass = 24;
    int g_intervalMs = 350;
    std::atomic<int> g_lastMatched{ 0 };
    std::atomic<int> g_lastPacked{ 0 };
    std::atomic<int> g_lastStaged{ 0 };
    std::atomic<int> g_lastTransferAttempts{ 0 };
    std::atomic<int> g_lastTransferDispatches{ 0 };
    std::atomic<int> g_lastTransferAccepted{ 0 };
    std::atomic<int> g_lastLocationAttempts{ 0 };
    std::atomic<int> g_lastLocationAccepted{ 0 };
    std::atomic<bool> g_taskPending{ false };
    std::atomic<uintptr_t> g_backpackActor{ 0 };
    std::atomic<uintptr_t> g_backpackInventory{ 0 };
    std::atomic<uintptr_t> g_backpackWorld{ 0 };
    ULONGLONG g_lastRun = 0;
    uintptr_t g_pickupClass = 0;
    std::mutex g_statusMutex;
    char g_status[256] = "Loot backpack ready.";

    void SetStatus(const char* format, ...)
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        va_list args;
        va_start(args, format);
        std::vsnprintf(g_status, sizeof(g_status), format, args);
        va_end(args);
    }

    bool SetActorLocation(uintptr_t actor, const FVector& location)
    {
        if (!actor || !location.IsFinite())
            return false;
        alignas(16) std::array<uint8_t, 0x128> params{};
        std::memcpy(params.data(), &location, sizeof(location));
        params[0x18] = 0;
        params[0x120] = 1;
        const bool dispatched = GameAccess::InvokeFunctionRaw(actor,
            FunctionIndices::Actor_K2_SetActorLocation,
            params.data(), params.size());
        if (dispatched && params[0x121] != 0)
            return true;

        // Some replicated pickup actors reject K2_SetActorLocation while their
        // scene root is being initialized. The combined transform entry point
        // follows the same engine path and succeeds once the root is registered.
        alignas(16) std::array<uint8_t, 0x140> transformParams{};
        std::memcpy(transformParams.data() + 0x00, &location,
                    sizeof(location));
        transformParams[0x30] = 0;   // bSweep
        transformParams[0x138] = 1;  // bTeleport
        const bool transformDispatched = GameAccess::InvokeFunctionRaw(actor,
            FunctionIndices::Actor_K2_SetActorLocationAndRotation,
            transformParams.data(), transformParams.size());
        return transformDispatched && transformParams[0x139] != 0;
    }

    uintptr_t PickupClass()
    {
        if (!g_pickupClass)
            g_pickupClass = GameAccess::GetObjectByIndex(
                ObjectIndices::PickUpActorClass);
        return g_pickupClass;
    }

    FVector BackpackDestination()
    {
        auto camera = GameAccess::GetRenderCamera();
        if (!camera.Valid)
            camera = GameAccess::GetCamera();
        FVector base{};
        if (!GameAccess::GetActorLocation(GameAccess::GetLocalPawn(), base))
            base = camera.Location;
        constexpr double DegToRad = 3.14159265358979323846 / 180.0;
        const double yaw = camera.Rotation.Yaw * DegToRad;
        return base + FVector(
            std::cos(yaw) * static_cast<double>(g_dropDistanceMeters) * 100.0,
            std::sin(yaw) * static_cast<double>(g_dropDistanceMeters) * 100.0,
            static_cast<double>(g_dropHeightMeters) * 100.0);
    }

    FVector StagingDestination(const FVector& backpackLocation, int ordinal)
    {
        constexpr double GoldenAngle = 2.39996322972865332;
        const double ring = 45.0 + 8.0 * static_cast<double>(ordinal % 5);
        const double angle = GoldenAngle * static_cast<double>(ordinal);
        return backpackLocation + FVector(std::cos(angle) * ring,
            std::sin(angle) * ring, 20.0 + static_cast<double>(ordinal % 3) * 8.0);
    }

    uintptr_t SpawnLootBackpack(uintptr_t world, uintptr_t owner,
                                const FVector& location)
    {
        const uintptr_t backpackClass = GameAccess::GetObjectByIndex(
            ObjectIndices::BPAVSDeltaBackpackClass);
        if (!backpackClass || !owner)
            return 0;

        // InventoryComponent::SpawnItem is the game's own construction path. It
        // creates the pickup actor with its replicated inventory, item payload,
        // mesh and interaction state initialized. A raw GameplayStatics spawn can
        // produce a non-zero actor pointer that has no usable component, which is
        // exactly the zero-packed/zero-staged failure shown by the diagnostics UI.
        const uintptr_t playerInventory = GameAccess::GetInventoryComponent();
        if (playerInventory)
        {
            alignas(16) std::array<uint8_t, 0x18> spawnParams{};
            std::memcpy(spawnParams.data() + 0x00, &backpackClass,
                        sizeof(backpackClass));
            std::memcpy(spawnParams.data() + 0x08, &owner, sizeof(owner));
            if (GameAccess::InvokeFunctionRaw(playerInventory,
                    FunctionIndices::InventoryComponent_SpawnItem,
                    spawnParams.data(), spawnParams.size()))
            {
                uintptr_t actor = 0;
                std::memcpy(&actor, spawnParams.data() + 0x10, sizeof(actor));
                if (actor)
                {
                    // The actor is already game-owned; only placement remains.
                    // Keep it even if UE reports a blocked sweep so inventory
                    // collection can still proceed through its live component.
                    SetActorLocation(actor, location);
                    return actor;
                }
            }
        }

        const uintptr_t gameplayStatics = GameAccess::GetObjectByIndex(
            ObjectIndices::DefaultGameplayStatics);
        if (!gameplayStatics || !backpackClass || !world || !owner)
            return 0;

        // This bounded 32x32 grid provides 1024 storage cells without asking the
        // inventory UI to allocate a literally unbounded container.
        struct FIntPoint { int32_t X = 32; int32_t Y = 32; } largeGrid{};
        for (const int32_t index : { ObjectIndices::BackpackStorageSettings0,
                                     ObjectIndices::BackpackStorageSettings1 })
        {
            const uintptr_t settings = GameAccess::GetObjectByIndex(index);
            if (!settings || !Memory::Write(settings +
                    Offsets::InventorySpatialContainerSettings_ContainerSize,
                    largeGrid))
                return 0;
        }

        FTransform transform{};
        transform.Translation = location;
        alignas(16) std::array<uint8_t, 0x90> beginParams{};
        std::memcpy(beginParams.data() + 0x00, &world, sizeof(world));
        std::memcpy(beginParams.data() + 0x08, &backpackClass,
                    sizeof(backpackClass));
        std::memcpy(beginParams.data() + 0x10, &transform, sizeof(transform));
        beginParams[0x70] = 1;
        std::memcpy(beginParams.data() + 0x78, &owner, sizeof(owner));
        beginParams[0x80] = 0;
        if (!GameAccess::InvokeFunctionRaw(gameplayStatics,
                FunctionIndices::GameplayStatics_BeginDeferredActorSpawnFromClass,
                beginParams.data(), beginParams.size()))
            return 0;

        uintptr_t actor = 0;
        std::memcpy(&actor, beginParams.data() + 0x88, sizeof(actor));
        if (!actor)
            return 0;

        alignas(16) std::array<uint8_t, 0x80> finishParams{};
        std::memcpy(finishParams.data() + 0x00, &actor, sizeof(actor));
        std::memcpy(finishParams.data() + 0x10, &transform, sizeof(transform));
        finishParams[0x70] = 0;
        if (!GameAccess::InvokeFunctionRaw(gameplayStatics,
                FunctionIndices::GameplayStatics_FinishSpawningActor,
                finishParams.data(), finishParams.size()))
            return 0;
        uintptr_t finished = 0;
        std::memcpy(&finished, finishParams.data() + 0x78, sizeof(finished));
        return finished ? finished : actor;
    }

    bool TransferPickup(uintptr_t pickupActor, uintptr_t backpackInventory)
    {
        g_lastTransferAttempts.fetch_add(1);
        const uintptr_t sourceInventory = Memory::Read<uintptr_t>(pickupActor +
            Offsets::PickUpActor_InventoryComponent);
        if (!sourceInventory || !backpackInventory)
            return false;

        std::array<uint8_t, ContainerItemSize> item{};
        const uintptr_t itemAddress = sourceInventory +
            Offsets::InventoryComponent_PickUpContainerItem +
            PickUpContainerItem_ContainerItem;
        if (!Memory::ReadRaw(itemAddress, item.data(), item.size()))
            return false;
        uintptr_t definition = 0;
        std::memcpy(&definition, item.data() + 0x10, sizeof(definition));
        if (!definition || !Memory::IsReadable(definition, sizeof(uintptr_t)))
            return false;

        alignas(16) std::array<uint8_t, 0x108> params{};
        std::memcpy(params.data() + 0x00, &sourceInventory,
                    sizeof(sourceInventory));
        std::memcpy(params.data() + 0x08, item.data(), item.size());
        // `bStacked` means merge into an existing stack. A pickup transfer is an
        // ownership move, so the game's own TryAddItem callers leave this false;
        // setting it true causes a valid item payload to be rejected by the
        // backpack container and was the reason every pass reported packed=0.
        params[0x100] = 0;
        const bool dispatched = GameAccess::InvokeFunctionRaw(backpackInventory,
            FunctionIndices::InventoryComponent_TryAddItem,
            params.data(), params.size());
        if (dispatched)
            g_lastTransferDispatches.fetch_add(1);
        const bool accepted = dispatched && params[0x101] != 0;
        if (accepted)
            g_lastTransferAccepted.fetch_add(1);
        return accepted;
    }

    void RunMagnet(bool manual)
    {
        if (g_taskPending.load())
        {
            SetStatus("Loot backpack: previous game-thread transfer is still running");
            return;
        }

        const uintptr_t pawn = GameAccess::GetLocalPawn();
        const uintptr_t world = GameAccess::GetWorld();
        const uintptr_t pickupClass = PickupClass();
        if (!pawn || !world || !pickupClass)
        {
            SetStatus("Loot backpack: waiting for pawn/world/PickUpActor class");
            return;
        }

        FVector local{};
        if (!GameAccess::GetActorLocation(pawn, local))
        {
            SetStatus("Loot backpack: local position unavailable");
            return;
        }

        std::vector<uintptr_t> pickups;
        const int limit = std::clamp(g_maxPerPass, 1, 64);
        const double maxCm = static_cast<double>(
            std::clamp(g_rangeMeters, 2.0f, 150.0f)) * 100.0;
        const uintptr_t existingBackpack = g_backpackWorld.load() == world ?
            g_backpackActor.load() : 0;
        int matched = 0;
        for (const uintptr_t actor : GameAccess::GetActors())
        {
            if (!actor || actor == pawn || actor == existingBackpack ||
                !GameAccess::IsInstanceOf(actor, pickupClass))
                continue;
            FVector location{};
            if (!GameAccess::GetActorLocation(actor, location))
                continue;
            const double distance = local.Distance(location);
            if (!std::isfinite(distance) || distance > maxCm)
                continue;
            ++matched;
            if (static_cast<int>(pickups.size()) < limit)
                pickups.push_back(actor);
        }
        g_lastMatched.store(matched);
        g_lastPacked.store(0);
        g_lastStaged.store(0);
        g_lastTransferAttempts.store(0);
        g_lastTransferDispatches.store(0);
        g_lastTransferAccepted.store(0);
        g_lastLocationAttempts.store(0);
        g_lastLocationAccepted.store(0);
        if (pickups.empty() && !manual)
        {
            SetStatus("Loot backpack: no pickup actors in %.0f m", g_rangeMeters);
            return;
        }

        const FVector destination = BackpackDestination();
        g_taskPending.store(true);
        if (!QueueGameThreadTask([pickups = std::move(pickups), destination,
                                  world, pawn, manual]()
            {
                int packed = 0;
                int staged = 0;
                FVector containerLocation = destination;
                uintptr_t backpack = g_backpackWorld.load() == world ?
                    g_backpackActor.load() : 0;
                if (!backpack || !Memory::IsReadable(backpack, sizeof(uintptr_t)))
                {
                    backpack = SpawnLootBackpack(world, pawn, destination);
                    g_backpackActor.store(backpack);
                    g_backpackWorld.store(backpack ? world : 0);
                    g_backpackInventory.store(0);
                }
                else if (manual)
                {
                    SetActorLocation(backpack, destination);
                }
                else
                {
                    GameAccess::GetActorLocation(backpack, containerLocation);
                }

                const uintptr_t backpackInventory = backpack ?
                    Memory::Read<uintptr_t>(backpack +
                        Offsets::PickUpActor_InventoryComponent) : 0;
                g_backpackInventory.store(backpackInventory);
                int ordinal = 0;
                for (const uintptr_t pickup : pickups)
                {
                    if (!pickup || pickup == backpack)
                        continue;
                    if (backpackInventory && TransferPickup(pickup,
                                                            backpackInventory))
                    {
                        ++packed;
                        // TryAddItem moves the live ContainerItem into the
                        // backpack inventory, but the original world pickup can
                        // remain until its replicated pickup state catches up.
                        // Put that actor at the container as a visible fallback;
                        // it prevents successfully packed items from being left
                        // scattered across the raid while the server update runs.
                        g_lastLocationAttempts.fetch_add(1);
                        if (SetActorLocation(pickup, containerLocation))
                            g_lastLocationAccepted.fetch_add(1);
                        continue;
                    }
                    g_lastLocationAttempts.fetch_add(1);
                    if (SetActorLocation(pickup,
                            StagingDestination(containerLocation, ordinal++)))
                    {
                        g_lastLocationAccepted.fetch_add(1);
                        ++staged;
                    }
                }

                g_lastPacked.store(packed);
                g_lastStaged.store(staged);
                if (backpack && backpackInventory)
                    SetStatus("Loot backpack: %d packed, %d staged | transfer %d/%d | move %d/%d%s",
                        packed, staged, g_lastTransferAccepted.load(),
                        g_lastTransferAttempts.load(), g_lastLocationAccepted.load(),
                        g_lastLocationAttempts.load(), manual ? " (manual)" : "");
                else
                    SetStatus("Backpack spawn failed; %d pickups staged in front%s",
                        staged, manual ? " (manual)" : "");
                g_taskPending.store(false);
            }, false))
        {
            g_taskPending.store(false);
            SetStatus("Loot backpack: game-thread queue unavailable");
        }
    }
}

namespace ItemMagnet
{
    bool enabled = false;
    int selectedIndex = 0;

    void ProcessTick()
    {
        if (!enabled)
            return;
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG interval = static_cast<ULONGLONG>(
            std::clamp(g_intervalMs, 150, 1500));
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
        g_lastMatched.store(0);
        g_lastPacked.store(0);
        g_lastStaged.store(0);
        g_lastTransferAttempts.store(0);
        g_lastTransferDispatches.store(0);
        g_lastTransferAccepted.store(0);
        g_lastLocationAttempts.store(0);
        g_lastLocationAccepted.store(0);
        g_backpackActor.store(0);
        g_backpackInventory.store(0);
        g_backpackWorld.store(0);
    }

    void PullAllItems() { RunMagnet(true); }
    void PullHighValueItems() { RunMagnet(true); }
    void PullSelectedItem() { RunMagnet(true); }

    void RenderTab()
    {
        const uintptr_t pickupClass = PickupClass();
        ImGui::Text("Loot Backpack | pickup class: %s | cached actors: %d",
            pickupClass ? "VALID" : "WAIT",
            static_cast<int>(GameAccess::GetActors().size()));
        ImGui::TextWrapped("Spawns a real BP_AVS_Delta_Backpack_C in front of the player on Unreal's game thread, expands its storage to a bounded 32x32 grid, and moves each pickup's live ContainerItem into that inventory. Items that reject transfer are staged beside the backpack instead of being lost.");

        ImGui::Checkbox("Continuous loot collection", &enabled);
        ImGui::SliderFloat("Pull range", &g_rangeMeters, 5.0f, 150.0f, "%.0f m");
        ImGui::SliderFloat("Backpack distance", &g_dropDistanceMeters, 0.8f, 5.0f, "%.1f m");
        ImGui::SliderFloat("Backpack height", &g_dropHeightMeters, 0.0f, 1.5f, "%.1f m");
        ImGui::SliderInt("Max pickups per pass", &g_maxPerPass, 1, 64);
        ImGui::SliderInt("Collection interval", &g_intervalMs, 150, 1500, "%d ms");

        if (ImGui::Button("Spawn backpack + collect nearby loot"))
            PullAllItems();
        ImGui::SameLine();
        if (ImGui::Button("Performance preset"))
        {
            g_rangeMeters = 30.0f;
            g_maxPerPass = 16;
            g_intervalMs = 500;
        }
        ImGui::SameLine();
        if (ImGui::Button("Aggressive preset"))
        {
            g_rangeMeters = 100.0f;
            g_maxPerPass = 48;
            g_intervalMs = 250;
        }

        ImGui::Text("Backpack: 0x%llX | inventory: 0x%llX | task: %s",
            static_cast<unsigned long long>(g_backpackActor.load()),
            static_cast<unsigned long long>(g_backpackInventory.load()),
            g_taskPending.load() ? "RUNNING" : "IDLE");
        ImGui::Text("Last pass: matched %d | packed %d | staged %d",
            g_lastMatched.load(), g_lastPacked.load(), g_lastStaged.load());
        ImGui::Text("Transfers: accepted %d / dispatched %d / attempted %d | moves: %d / %d",
            g_lastTransferAccepted.load(), g_lastTransferDispatches.load(),
            g_lastTransferAttempts.load(), g_lastLocationAccepted.load(),
            g_lastLocationAttempts.load());
        char status[sizeof(g_status)]{};
        {
            std::lock_guard<std::mutex> lock(g_statusMutex);
            std::memcpy(status, g_status, sizeof(status));
        }
        ImGui::TextWrapped("%s", status);
        ImGui::TextDisabled("Continuous mode is opt-in and capped at 64 inventory transfers per pass to protect frame time.");
    }

    void Render() { RenderTab(); }
}
