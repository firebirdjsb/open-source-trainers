#include "DiagnosticsService.h"

#include "Aimbot.h"
#include "InventoryService.h"
#include "../Memory/Memory.h"
#include "../hooks/PresentHook.h"
#include "../sdk/GameAccess.h"
#include "../sdk/Offsets.h"

#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <string>

namespace
{
    std::string OutputPath()
    {
        char exePath[MAX_PATH]{};
        const DWORD length = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string path = length ? std::string(exePath, length) : std::string(".");
        const std::size_t slash = path.find_last_of("\\/");
        if (slash != std::string::npos)
            path.resize(slash + 1);
        else
            path = ".\\";
        path += "IncursionCheat_FullDiagnostics.txt";
        return path;
    }

    void Hex(std::ofstream& out, const char* name, uintptr_t value)
    {
        out << std::left << std::setw(36) << name << " 0x"
            << std::right << std::hex << std::uppercase << std::setw(16)
            << std::setfill('0') << static_cast<unsigned long long>(value)
            << std::setfill(' ') << std::dec << "\n";
    }

    const char* YesNo(bool value) { return value ? "YES" : "NO"; }
}

namespace DiagnosticsService
{
    bool WriteFullDump(char* outPath, std::size_t outPathSize)
    {
        const std::string path = OutputPath();
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out)
            return false;

        SYSTEMTIME time{};
        GetLocalTime(&time);
        out << "INCURSION: RED RIVER - FULL CHEAT RUNTIME DIAGNOSTIC\n";
        out << "Generated: " << time.wYear << '-' << std::setfill('0') << std::setw(2)
            << time.wMonth << '-' << std::setw(2) << time.wDay << ' '
            << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute
            << ':' << std::setw(2) << time.wSecond << std::setfill(' ') << "\n\n";

        out << "=== RENDER / HOOK ===\n";
        out << "Hook installed:       " << YesNo(IsHookInstalled()) << "\n";
        out << "Command queue:        " << YesNo(HasCapturedCommandQueue()) << "\n";
        out << "Renderer initialized: " << YesNo(IsRendererInitialized()) << "\n";
        out << "Diagnostic caller thread: " << GetCurrentThreadId() << "\n";
        out << "Game window thread:       " << GetGameWindowThreadId() << "\n";
        out << "Last inventory-task thread: " << GetLastGameTaskThreadId() << "\n";
        Hex(out, "EXE module base", Memory::GetBase());

        const auto& d = GameAccess::GetDiagnostics();
        out << "\n=== GAME ACCESS ===\n";
        out << "Failure stage: " << (d.FailureStage ? d.FailureStage : "<null>") << "\n";
        out << "World source: " << GameAccess::SourceName(d.WorldSource)
            << " | active raid: " << YesNo(d.WorldIsActiveRaid)
            << " | candidates: " << d.WorldCandidateCount << "\n";
        out << "GameInstance source: " << GameAccess::SourceName(d.GameInstanceSource)
            << " | type valid: " << YesNo(d.GameInstanceTypeValid) << "\n";
        Hex(out, "GWorld slot", d.GWorldSlot);
        Hex(out, "Raw GWorld", d.RawGWorld);
        Hex(out, "Resolved World", d.World);
        Hex(out, "PersistentLevel", d.PersistentLevel);
        Hex(out, "ULevel Actors data", d.ActorArrayData);
        out << "Actors: " << d.ActorCount << '/' << d.ActorCapacity
            << " | active IRR: " << d.ActiveCharacterCount
            << " | scanned IRR: " << d.ScannedCharacterCount << "\n";
        Hex(out, "GameEngine", d.GameEngine);
        Hex(out, "ViewportClient", d.ViewportClient);
        Hex(out, "World GameInstance", d.WorldGameInstance);
        Hex(out, "Resolved GeneralGameInstance", d.GameInstance);
        Hex(out, "LocalPlayers data", d.LocalPlayersData);
        out << "LocalPlayers: " << d.LocalPlayersCount << '/' << d.LocalPlayersCapacity << "\n";
        Hex(out, "LocalPlayer", d.LocalPlayer);
        Hex(out, "PlayerController", d.PlayerController);
        Hex(out, "Controller Pawn", d.ControllerPawn);
        Hex(out, "AcknowledgedPawn", d.AcknowledgedPawn);
        Hex(out, "Resolved Pawn", d.Pawn);
        Hex(out, "CameraManager", d.CameraManager);
        out << "Pawn type valid: " << YesNo(d.PawnTypeValid) << "\n";

        out << "\n=== GUOBJECTARRAY / PROCESSEVENT ===\n";
        Hex(out, "Configured GObjects address", d.GObjectsAddress);
        Hex(out, "Resolved object array", d.ResolvedObjectArray);
        Hex(out, "Object chunk table", d.ObjectChunkTable);
        out << "Objects: " << d.ObjectCount << '/' << d.ObjectCapacity
            << " | stride 0x" << std::hex << d.ObjectItemStride << std::dec
            << " | probe " << d.ObjectArrayProbeScore << "/5"
            << " | valid " << YesNo(d.ObjectArrayValid)
            << " | section scan " << YesNo(d.ObjectArrayUsedSectionScan) << "\n";
        out << "Typed objects: worlds=" << d.ObjectWorldCount
            << " engines=" << d.ObjectGameEngineCount
            << " viewports=" << d.ObjectViewportCount
            << " localPlayers=" << d.ObjectLocalPlayerCount
            << " controllers=" << d.ObjectControllerCount
            << " gameInstances=" << d.ObjectGameInstanceCount
            << " characters=" << d.ObjectCharacterCount
            << " weapons=" << d.ObjectWeaponCount << "\n";
        Hex(out, "ProcessEvent", d.ProcessEventAddress);
        Hex(out, "Last UFunction", d.LastFunctionObject);
        out << "ProcessEvent valid: " << YesNo(d.ProcessEventValid)
            << " | last index: 0x" << std::hex << d.LastFunctionIndex << std::dec
            << " | last call succeeded: " << YesNo(d.LastProcessEventCallSucceeded) << "\n";

        out << "\n=== TEAM / HOSTILES ===\n";
        Hex(out, "Local TeamComponent", d.LocalTeamComponent);
        Hex(out, "Hostiles array data", d.HostileArrayData);
        out << "Hostiles: " << d.HostileArrayCount << '/' << d.HostileArrayCapacity
            << " | matched active: " << d.HostileCharacterCount
            << " | array valid: " << YesNo(d.HostileArrayValid) << "\n";

        out << "\n=== WEAPON / PLAYER SUBOBJECTS ===\n";
        Hex(out, "Equipped weapon", d.EquippedWeapon);
        Hex(out, "WeaponComponent", d.WeaponComponent);
        Hex(out, "Ballistic barrel", d.BallisticBarrel);
        out << "Weapon type valid: " << YesNo(d.WeaponTypeValid)
            << " | component valid: " << YesNo(d.WeaponComponentTypeValid)
            << " | projectile cm/s: " << d.ProjectileSpeedCmPerSecond << "\n";
        Hex(out, "SenseStimulusComponent", d.SenseStimulusComponent);
        Hex(out, "FirstPersonStamina", d.StaminaObject);
        Hex(out, "StaminaAttribute", d.StaminaAttribute);
        const auto& staminaAttributes = GameAccess::GetStaminaAttributes();
        out << "Owned stamina attributes: " << staminaAttributes.size()
            << " (character/sprint + arms/aim expected)\n";
        for (std::size_t index = 0; index < staminaAttributes.size(); ++index)
        {
            const uintptr_t attribute = staminaAttributes[index];
            const std::string label = "Stamina channel[" + std::to_string(index) + "] attribute";
            Hex(out, label.c_str(), attribute);

            float staminaCurrent = 0.0f;
            float staminaMax = 0.0f;
            const bool staminaCurOk = Memory::TryRead(attribute +
                Offsets::SimpleGameplayAttribute_CurrentData +
                Offsets::SimpleAttributeData_BaseValue, staminaCurrent);
            const bool staminaMaxOk = Memory::TryRead(attribute +
                Offsets::SimpleGameplayAttribute_CurrentData +
                Offsets::SimpleAttributeData_MaxValue, staminaMax);
            out << "  raw current-data snapshot: value=" << staminaCurrent << " ("
                << YesNo(staminaCurOk) << ") max=" << staminaMax << " ("
                << YesNo(staminaMaxOk) << ")\n";
        }
        out << "Infinite stamina implementation: SAFE reflected SetBaseValue only; raw stamina writes disabled.\n";

        out << "\n=== INVENTORY / STASH ===\n";
        const uintptr_t playerInventory = InventoryService::GetPlayerInventory();
        const uintptr_t stashInventory = InventoryService::GetStashInventory();
        const auto playerProbe = InventoryService::ProbeInventory(playerInventory);
        const auto stashProbe = InventoryService::ProbeInventory(stashInventory);
        Hex(out, "Player InventoryComponent", playerInventory);
        Hex(out, "Player MainContainers data", playerProbe.MainArrayData);
        out << "Player containers: " << playerProbe.MainContainerCount << '/'
            << playerProbe.MainContainerCapacity << " | item records: "
            << playerProbe.TotalContainerItems << " | valid: " << YesNo(playerProbe.MainArrayValid) << "\n";
        Hex(out, "Stash InventoryComponent", stashInventory);
        Hex(out, "Stash MainContainers data", stashProbe.MainArrayData);
        out << "Stash containers: " << stashProbe.MainContainerCount << '/'
            << stashProbe.MainContainerCapacity << " | item records: "
            << stashProbe.TotalContainerItems << " | valid: " << YesNo(stashProbe.MainArrayValid) << "\n";
        const auto inventoryResult = InventoryService::GetLastResult();
        out << "Last insertion: success=" << YesNo(inventoryResult.Success)
            << " backend=" << inventoryResult.Backend
            << " component=0x" << std::hex << inventoryResult.TargetComponent << std::dec
            << " container=" << inventoryResult.ContainerIndex
            << " mainContainers=" << inventoryResult.MainContainerCount
            << " type=" << inventoryResult.DefaultItemType
            << " dispatches=" << inventoryResult.DispatchCount
            << " count=" << inventoryResult.CountBefore << "->" << inventoryResult.CountAfter
            << " records=" << inventoryResult.ItemRecordsBefore << "->"
            << inventoryResult.ItemRecordsAfter
            << " completeWeapon=" << YesNo(inventoryResult.CompleteWeapon)
            << " preset=0x" << std::hex << inventoryResult.PresetObject << std::dec
            << " expectedAttachments=" << inventoryResult.ExpectedAttachments
            << " returnedDef=0x" << std::hex << inventoryResult.ReturnedDefinition << std::dec
            << " canAdd=" << YesNo(inventoryResult.CanAddBuiltItem)
            << " tryAdd=" << YesNo(inventoryResult.TryAddReturned) << "\n";
        out << "Last insertion message: " << inventoryResult.Message << "\n";

        out << "\n=== AIMBOT ===\n";
        const auto& a = Aimbot::GetDiagnostics();
        Hex(out, "Aim controller", a.Controller);
        Hex(out, "Target actor", a.TargetActor);
        Hex(out, "Target BodyComponent", a.TargetBodyComponent);
        out << "Target found=" << YesNo(a.TargetFound)
            << " RMB=" << YesNo(a.RmbHeld)
            << " LMB=" << YesNo(a.LmbHeld)
            << " activation=" << YesNo(a.ActivationHeld)
            << " attempted=" << YesNo(a.AimAttempted)
            << " sticky=" << YesNo(a.StickyTarget)
            << " mouseInput=" << YesNo(a.UsedMouseInput)
            << " setControlRotation=" << YesNo(a.UsedSetControlRotationFunction) << "\n";
        out << "Scan counts: characters=" << a.CharactersScanned
            << " enemies=" << a.EnemyCandidates
            << " living=" << a.LivingCandidates
            << " range=" << a.DistanceCandidates
            << " projected=" << a.ProjectedTargets
            << " inFov=" << a.InFovTargets
            << " liveBodyPoints=" << a.LiveBodyTargets
            << " poseAware=" << a.PoseAwareTargets
            << " poseFailures=" << a.PoseAwareFailures << "\n";
        const auto visibility = GameAccess::GetVisibilityDiagnostics();
        out << "Exposure candidates: known=" << a.VisibilityKnownTargets
            << " visible=" << a.VisibilityVisibleTargets
            << " hidden=" << a.VisibilityHiddenTargets
            << " pending=" << a.VisibilityUnknownTargets
            << " exposedPoint=" << YesNo(a.UsedExposedPoint) << "\n";
        out << "Exposure cache: actors=" << visibility.CachedActors
            << " visible=" << visibility.VisibleActors
            << " actorStateKnown=" << visibility.ActorStateKnown
            << " destroyed=" << visibility.DestroyedActors
            << " queued=" << visibility.QueuedActors
            << " taskPending=" << YesNo(visibility.TaskPending)
            << " lastBatch=" << visibility.LastRequestedActors
            << " lastVisible=" << visibility.LastVisibleActors
            << " exactTrace=" << visibility.LastLineTraceActors
            << " exactClear=" << visibility.LastLineTraceVisibleActors
            << " nativeLOS=" << visibility.LastNativeLosActors
            << " sphereFallback=" << visibility.LastSphereActors
            << " thread=0x" << std::hex << visibility.LastSampleThreadId
            << std::dec << "\n";
        out << "Target world={" << a.TargetWorld.X << ',' << a.TargetWorld.Y << ','
            << a.TargetWorld.Z << "} velocity={" << a.TargetVelocity.X << ','
            << a.TargetVelocity.Y << ',' << a.TargetVelocity.Z << "}\n";

        out << "\n=== KEY CURRENT OFFSETS ===\n";
        out << "GWorld RVA=0x" << std::hex << Offsets::GWorld
            << " GObjects RVA=0x" << Offsets::GObjects
            << " OwningGameInstance=0x" << Offsets::UWorld_OwningGameInstance
            << " MainContainers=0x" << Offsets::InventoryComponent_MainContainers
            << std::dec << "\n";

        out.flush();
        const bool ok = out.good();
        out.close();
        if (outPath && outPathSize)
            std::snprintf(outPath, outPathSize, "%s", path.c_str());
        return ok;
    }
}
