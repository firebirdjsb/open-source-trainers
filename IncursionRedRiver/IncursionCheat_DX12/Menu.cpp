#include "Menu.h"

#include "CheatManager.h"
#include "Memory/Memory.h"
#include "hooks/PresentHook.h"
#include "sdk/GameAccess.h"
#include "sdk/Offsets.h"
#include "Features/ESP.h"
#include "Features/Aimbot.h"
#include "Features/ItemMagnet.h"
#include "Features/Spawner.h"
#include "Features/UECheats.h"
#include "Features/ProfileTools.h"
#include "Features/TacticalTools.h"
#include "Features/MovementTools.h"
#include "Features/WorldTools.h"
#include "Features/DiagnosticsService.h"
#include "imgui/imgui.h"

namespace
{
    char g_diagnosticDumpPath[520] = "Not written this session.";
    bool g_diagnosticDumpOk = false;

    void WriteDiagnosticDump()
    {
        g_diagnosticDumpOk = DiagnosticsService::WriteFullDump(
            g_diagnosticDumpPath, sizeof(g_diagnosticDumpPath));
    }

    void Address(const char* label, uintptr_t value)
    {
        ImGui::Text("%-30s 0x%016llX", label,
            static_cast<unsigned long long>(value));
    }

    void RenderDiagnostics()
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.42f, 0.68f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.54f, 0.82f, 1.00f));
        if (ImGui::Button("CREATE FULL RUNTIME DUMP", ImVec2(270.0f, 38.0f)))
            WriteDiagnosticDump();
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::TextColored(g_diagnosticDumpOk ?
            ImVec4(0.35f, 0.95f, 0.62f, 1.0f) : ImVec4(0.90f, 0.72f, 0.35f, 1.0f),
            "%s", g_diagnosticDumpOk ? "DUMP WRITTEN" : "READY");
        ImGui::TextWrapped("Output: %s", g_diagnosticDumpPath);
        ImGui::TextDisabled("One-shot snapshot: hook/render state, UE chain, GUObjectArray, ProcessEvent, inventory/stash, stamina, active characters and aimbot.");
        ImGui::Separator();

        const auto& d = GameAccess::GetDiagnostics();
        ImGui::Text("Failure stage: %s", d.FailureStage);
        ImGui::Text("World source: %s | active raid: %s | candidates: %d",
            GameAccess::SourceName(d.WorldSource), d.WorldIsActiveRaid ? "YES" : "NO",
            d.WorldCandidateCount);
        ImGui::Text("GameInstance source: %s | type: %s",
            GameAccess::SourceName(d.GameInstanceSource),
            d.GameInstanceTypeValid ? "GeneralGameInstance" : "INVALID");
        ImGui::Text("GUObjectArray: %s | probe: %d/5 | source: %s",
            d.ObjectArrayValid ? "OK" : "FAILED", d.ObjectArrayProbeScore,
            d.ObjectArrayUsedSectionScan ? "PE section scan" : "configured RVA");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("World / engine", ImGuiTreeNodeFlags_DefaultOpen))
        {
            Address("EXE base", d.ModuleBase);
            Address("configured GWorld slot", d.GWorldSlot);
            Address("raw GWorld candidate", d.RawGWorld);
            ImGui::Text("Raw GWorld pointer plausibility: %s",
                d.RawGWorldPlausible ? "READABLE" : "INVALID / STALE RVA");
            Address("resolved active World", d.World);
            Address("PersistentLevel", d.PersistentLevel);
            Address("Level::OwningWorld", d.LevelOwningWorld);
            Address("ULevel::Actors data", d.ActorArrayData);
            ImGui::Text("Actors: %d / %d | IRR active: %d | GUObject IRR: %d",
                d.ActorCount, d.ActorCapacity, d.ActiveCharacterCount,
                d.ScannedCharacterCount);
            Address("GameEngine", d.GameEngine);
            Address("Engine::GameInstance", d.EngineGameInstance);
            Address("GameViewportClient", d.ViewportClient);
            Address("Viewport::World", d.ViewportWorld);
            Address("Viewport::GameInstance", d.ViewportGameInstance);
        }

        if (ImGui::CollapsingHeader("Local runtime chain", ImGuiTreeNodeFlags_DefaultOpen))
        {
            Address("World::OwningGameInstance", d.WorldGameInstance);
            Address("resolved GeneralGameInstance", d.GameInstance);
            Address("LocalPlayers data", d.LocalPlayersData);
            ImGui::Text("LocalPlayers: %d / %d", d.LocalPlayersCount,
                d.LocalPlayersCapacity);
            Address("LocalPlayer", d.LocalPlayer);
            ImGui::Text("LocalPlayer source: %s", GameAccess::SourceName(d.LocalPlayerSource));
            Address("PlayerController", d.PlayerController);
            ImGui::Text("Controller source: %s | scanned: %d",
                GameAccess::SourceName(d.ControllerSource), d.ScannedControllerCount);
            Address("Controller::Player", d.ControllerPlayer);
            Address("Controller::Pawn", d.ControllerPawn);
            Address("Controller::AcknowledgedPawn", d.AcknowledgedPawn);
            Address("resolved Pawn", d.Pawn);
            ImGui::Text("Pawn IRRBaseCharacter type: %s", d.PawnTypeValid ? "OK" : "INVALID");
            Address("PlayerCameraManager", d.CameraManager);
            Address("Pawn::TeamComponent", d.LocalTeamComponent);
            ImGui::Text("TeamComponent type: %s",
                d.LocalTeamComponentTypeValid ? "OK" : "INVALID");
            Address("TeamComponent::Hostiles data", d.HostileArrayData);
            ImGui::Text("Hostiles: %d / %d | matched active IRR: %d | validation: %s",
                d.HostileArrayCount, d.HostileArrayCapacity,
                d.HostileCharacterCount, d.HostileArrayValid ? "OK" : "UNAVAILABLE");
            Address("Pawn::SenseStimulusComponent", d.SenseStimulusComponent);
            ImGui::Text("SenseStimulusComponent type: %s",
                d.SenseStimulusComponentTypeValid ? "OK" : "INVALID");
        }

        if (ImGui::CollapsingHeader("Weapon / inventory", ImGuiTreeNodeFlags_DefaultOpen))
        {
            Address("equipped BP_MasterWeapon", d.EquippedWeapon);
            ImGui::Text("Weapon type: %s", d.WeaponTypeValid ? "OK" : "INVALID");
            Address("WeaponComponent", d.WeaponComponent);
            ImGui::Text("WeaponComponent type: %s",
                d.WeaponComponentTypeValid ? "OK" : "INVALID");
            Address("player InventoryComponent", d.InventoryComponent);
            Address("local FirstPersonStamina", d.StaminaObject);
            Address("StaminaAttribute", d.StaminaAttribute);
            ImGui::Text("Owned stamina attributes: %d", d.StaminaAttributeCount);
            Address("local FirstPersonStamina_Arm", d.StaminaArmObject);
            Address("local FirstPersonWeaponRecoil", d.WeaponRecoilObject);
            Address("ProcessEvent", d.ProcessEventAddress);
            Address("last UFunction", d.LastFunctionObject);
            ImGui::Text("ProcessEvent: %s | last function index: 0x%X | call: %s",
                d.ProcessEventValid ? "VALID" : "UNVALIDATED", d.LastFunctionIndex,
                d.LastProcessEventCallSucceeded ? "OK" : "WAIT / FAILED");
        }

        if (ImGui::CollapsingHeader("GUObjectArray"))
        {
            Address("GObjects RVA address", d.GObjectsAddress);
            Address("resolved object-array root", d.ResolvedObjectArray);
            Address("object chunk table", d.ObjectChunkTable);
            ImGui::Text("Objects: %d / %d | item stride: 0x%X | probe: %s (%d/5)",
                d.ObjectCount, d.ObjectCapacity, d.ObjectItemStride,
                d.ObjectArrayValid ? "OK" : "FAILED", d.ObjectArrayProbeScore);
            ImGui::Text("Object-array source: %s",
                d.ObjectArrayUsedSectionScan ? "writable PE section scan" :
                                               "configured RVA");
            ImGui::Text("Typed: worlds %d | engines %d | viewports %d | local players %d",
                d.ObjectWorldCount, d.ObjectGameEngineCount, d.ObjectViewportCount,
                d.ObjectLocalPlayerCount);
            ImGui::Text("Typed: controllers %d | game instances %d | characters %d | weapons %d",
                d.ObjectControllerCount, d.ObjectGameInstanceCount,
                d.ObjectCharacterCount, d.ObjectWeaponCount);
        }

        if (ImGui::CollapsingHeader("Aimbot verification", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto& a = Aimbot::GetDiagnostics();
            const auto visibility = GameAccess::GetVisibilityDiagnostics();
            Address("aim controller", a.Controller);
            Address("target character", a.TargetActor);
            Address("target BodyComponent", a.TargetBodyComponent);
            ImGui::Text("Target: %s | RMB: %s | LMB: %s | active: %s | attempt: %s",
                a.TargetFound ? "FOUND" : "NONE", a.RmbHeld ? "DOWN" : "UP",
                a.LmbHeld ? "DOWN" : "UP", a.ActivationHeld ? "YES" : "NO",
                a.AimAttempted ? "YES" : "NO");
            ImGui::Text("Aim mode: %s", a.UsedMouseInput ? "MOUSE FALLBACK" :
                (a.UsedControllerLookInput ? "ENGINE LOOK INPUT" : "CONTROL ROTATION"));
            ImGui::Text("Aim submission/write: %s | next-frame rotation changed: %s",
                a.DirectWriteSucceeded ? "OK" : "NO",
                a.RotationChangedAfterAttempt ? "YES" : "NO");
            ImGui::Text("Scan: %d | hostile: %d | living: %d | range: %d | projected: %d | FOV: %d",
                a.CharactersScanned, a.EnemyCandidates, a.LivingCandidates,
                a.DistanceCandidates, a.ProjectedTargets, a.InFovTargets);
            ImGui::Text("Aim point: %s | body points: %d | body failures: %d | SetControlRotation: %s",
                a.TargetFound ? (a.UsedPoseAwareBodyPart ? "POSE-AWARE IRR BODY PART" :
                    (a.UsedEyeViewPoint ? "ACTOR EYE VIEWPOINT" :
                    (a.UsedCapsuleFallback ? "CAPSULE FALLBACK" : "LIVE BODY POINT"))) : "NONE",
                a.PoseAwareTargets, a.PoseAwareFailures,
                a.UsedSetControlRotationFunction ? "CALLED" : "NO");
            ImGui::Text("Target lock: %s | sticky: %s",
                a.TargetFound ? "ACQUIRED" : "WAIT",
                a.StickyTarget ? "YES" : "NO");
            ImGui::Text("Exposure candidates: known %d | visible %d | hidden %d | pending %d",
                a.VisibilityKnownTargets, a.VisibilityVisibleTargets,
                a.VisibilityHiddenTargets, a.VisibilityUnknownTargets);
            ImGui::Text("Exposure cache: %d actors / %d visible | actor state %d known (%d destroyed) | queued %d | task %s",
                visibility.CachedActors, visibility.VisibleActors,
                visibility.ActorStateKnown, visibility.DestroyedActors,
                visibility.QueuedActors, visibility.TaskPending ? "PENDING" : "IDLE");
            ImGui::Text("Exposure game thread: 0x%X | last batch %d / visible %d",
                visibility.LastSampleThreadId, visibility.LastRequestedActors,
                visibility.LastVisibleActors);
            ImGui::Text("Trace methods: exact %d / clear %d | native LOS %d | sphere fallback %d",
                visibility.LastLineTraceActors,
                visibility.LastLineTraceVisibleActors,
                visibility.LastNativeLosActors, visibility.LastSphereActors);
            ImGui::Text("Target world: { %.3f, %.3f, %.3f }",
                a.TargetWorld.X, a.TargetWorld.Y, a.TargetWorld.Z);
            ImGui::Text("Control before: { %.3f, %.3f, %.3f }",
                a.RotationBefore.Pitch, a.RotationBefore.Yaw, a.RotationBefore.Roll);
            ImGui::Text("Control observed: { %.3f, %.3f, %.3f }",
                a.RotationObserved.Pitch, a.RotationObserved.Yaw,
                a.RotationObserved.Roll);
        }
    }
}

namespace
{
    uintptr_t g_inputBlockedController = 0;
    uintptr_t g_inputBlockedPawn = 0;
    bool g_styleApplied = false;

    enum class MenuPage : int
    {
        ESP,
        Aimbot,
        Tactical,
        Player,
        Movement,
        World,
        Profile,
        Items,
        Spawner,
        Diagnostics,
        Notes
    };

    MenuPage g_page = MenuPage::ESP;

    void SetControllerIgnored(uintptr_t controller, bool ignored)
    {
        if (!controller || !Memory::IsReadable(controller, sizeof(uintptr_t)))
            return;

        struct BoolParam
        {
            bool Value = false;
        } params{};
        params.Value = ignored;
        GameAccess::InvokeFunctionRaw(controller, FunctionIndices::Controller_SetIgnoreLookInput,
            &params, sizeof(params));
        GameAccess::InvokeFunctionRaw(controller, FunctionIndices::Controller_SetIgnoreMoveInput,
            &params, sizeof(params));
    }

    void SetPawnInputEnabled(uintptr_t pawn, uintptr_t controller, bool enabled)
    {
        if (!pawn || !controller || !Memory::IsReadable(pawn, sizeof(uintptr_t)) ||
            !Memory::IsReadable(controller, sizeof(uintptr_t)))
            return;

        struct Params
        {
            uintptr_t PlayerController = 0;
        } params{};
        params.PlayerController = controller;
        GameAccess::InvokeFunctionRaw(pawn,
            enabled ? FunctionIndices::Actor_EnableInput : FunctionIndices::Actor_DisableInput,
            &params, sizeof(params));
    }

    void ApplyProfessionalStyle()
    {
        if (g_styleApplied || !ImGui::GetCurrentContext())
            return;
        g_styleApplied = true;

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(12.0f, 12.0f);
        style.FramePadding = ImVec2(10.0f, 6.0f);
        style.CellPadding = ImVec2(8.0f, 5.0f);
        style.ItemSpacing = ImVec2(9.0f, 7.0f);
        style.ItemInnerSpacing = ImVec2(7.0f, 5.0f);
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;
        style.WindowRounding = 10.0f;
        style.ChildRounding = 8.0f;
        style.FrameRounding = 6.0f;
        style.PopupRounding = 7.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 5.0f;
        style.TabRounding = 6.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;

        ImVec4* c = style.Colors;
        c[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.97f, 1.00f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.47f, 0.53f, 0.62f, 1.00f);
        c[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.047f, 0.065f, 0.985f);
        c[ImGuiCol_ChildBg] = ImVec4(0.050f, 0.066f, 0.091f, 0.965f);
        c[ImGuiCol_PopupBg] = ImVec4(0.045f, 0.060f, 0.083f, 0.985f);
        c[ImGuiCol_Border] = ImVec4(0.13f, 0.18f, 0.25f, 0.90f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.100f, 0.137f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.10f, 0.145f, 0.205f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.185f, 0.270f, 1.00f);
        c[ImGuiCol_TitleBg] = ImVec4(0.035f, 0.047f, 0.065f, 1.00f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.045f, 0.060f, 0.083f, 1.00f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.045f, 0.060f, 0.083f, 1.00f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.030f, 0.040f, 0.055f, 0.70f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.16f, 0.23f, 0.32f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.20f, 0.32f, 0.45f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.18f, 0.48f, 0.72f, 1.00f);
        c[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.72f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.18f, 0.62f, 0.92f, 1.00f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.25f, 0.76f, 1.00f, 1.00f);
        c[ImGuiCol_Button] = ImVec4(0.075f, 0.125f, 0.18f, 1.00f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.10f, 0.34f, 0.54f, 1.00f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.50f, 0.76f, 1.00f);
        c[ImGuiCol_Header] = ImVec4(0.075f, 0.115f, 0.16f, 1.00f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.10f, 0.28f, 0.43f, 1.00f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.10f, 0.43f, 0.66f, 1.00f);
        c[ImGuiCol_Separator] = ImVec4(0.13f, 0.18f, 0.25f, 0.90f);
        c[ImGuiCol_SeparatorHovered] = ImVec4(0.18f, 0.55f, 0.82f, 1.00f);
        c[ImGuiCol_SeparatorActive] = ImVec4(0.20f, 0.68f, 1.00f, 1.00f);
        c[ImGuiCol_ResizeGrip] = ImVec4(0.15f, 0.45f, 0.68f, 0.35f);
        c[ImGuiCol_ResizeGripHovered] = ImVec4(0.20f, 0.68f, 1.00f, 0.75f);
        c[ImGuiCol_ResizeGripActive] = ImVec4(0.20f, 0.68f, 1.00f, 1.00f);
        c[ImGuiCol_Tab] = ImVec4(0.060f, 0.085f, 0.12f, 1.00f);
        c[ImGuiCol_TabHovered] = ImVec4(0.10f, 0.34f, 0.54f, 1.00f);
        c[ImGuiCol_TabSelected] = ImVec4(0.10f, 0.43f, 0.66f, 1.00f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(0.10f, 0.43f, 0.66f, 0.50f);
    }

    const char* PageTitle(MenuPage page)
    {
        switch (page)
        {
        case MenuPage::ESP: return "ESP & VISUALS";
        case MenuPage::Aimbot: return "AIM ASSIST";
        case MenuPage::Tactical: return "TACTICAL OVERLAY";
        case MenuPage::Player: return "PLAYER";
        case MenuPage::Movement: return "MOVEMENT+";
        case MenuPage::World: return "WORLD & RAID";
        case MenuPage::Profile: return "PROFILE & ECONOMY";
        case MenuPage::Items: return "LOOT TOOLS";
        case MenuPage::Spawner: return "ITEM SPAWNER";
        case MenuPage::Diagnostics: return "DIAGNOSTICS";
        case MenuPage::Notes: return "BUILD NOTES";
        }
        return "IRR CONTROL CENTER";
    }

    const char* PageSubtitle(MenuPage page)
    {
        switch (page)
        {
        case MenuPage::ESP: return "Enemy visualization and shared exposure state";
        case MenuPage::Aimbot: return "Crosshair target acquisition and ballistic prediction";
        case MenuPage::Tactical: return "Radar, proximity awareness and off-screen threat indicators";
        case MenuPage::Player: return "Core player, weapon and survivability controls";
        case MenuPage::Movement: return "Advanced movement tuning and teleport bookmarks";
        case MenuPage::World: return "Raid, mission, AI and world-state operations";
        case MenuPage::Profile: return "Currency, progression and faction controls";
        case MenuPage::Items: return "Optimized pickup discovery and loot magnet";
        case MenuPage::Spawner: return "Verified inventory, stash and resource insertion";
        case MenuPage::Diagnostics: return "Live UE object chain and runtime validation";
        case MenuPage::Notes: return "Current build scope, safeguards and implementation notes";
        }
        return "";
    }

    void NavItem(const char* label, MenuPage page)
    {
        const bool selected = g_page == page;
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.10f, 0.43f, 0.66f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.12f, 0.50f, 0.74f, 1.00f));
        }
        if (ImGui::Selectable(label, selected, 0, ImVec2(0.0f, 32.0f)))
            g_page = page;
        if (selected)
            ImGui::PopStyleColor(2);
    }

    void NavGroup(const char* name)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("  %s", name);
        ImGui::Spacing();
    }

    void StatusValue(const char* label, bool ready)
    {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(ready ? ImVec4(0.30f, 0.90f, 0.58f, 1.00f) :
                                   ImVec4(1.00f, 0.67f, 0.22f, 1.00f),
                           "%s", ready ? "ONLINE" : "WAIT");
    }

    void RenderCurrentPage()
    {
        switch (g_page)
        {
        case MenuPage::ESP: ESP::Render(); break;
        case MenuPage::Aimbot: Aimbot::RenderTab(); break;
        case MenuPage::Tactical: TacticalTools::RenderTab(); break;
        case MenuPage::Player: UECheats::RenderTab(); break;
        case MenuPage::Movement: MovementTools::RenderTab(); break;
        case MenuPage::World: WorldTools::RenderTab(); break;
        case MenuPage::Profile: ProfileTools::RenderTab(); break;
        case MenuPage::Items: ItemMagnet::RenderTab(); break;
        case MenuPage::Spawner: Spawner::RenderTab(); break;
        case MenuPage::Diagnostics: RenderDiagnostics(); break;
        case MenuPage::Notes:
            ImGui::TextWrapped("This build preserves the working DX12 renderer, command-queue capture, runtime GameAccess resolver, ESP and aimbot paths. New systems are layered around those components and expensive scans remain throttled or cache-backed.");
            ImGui::Spacing();
            ImGui::TextWrapped("Inventory and stash insertion now use live IRRItemDefinition objects and InventoryComponent operations, with count-before/count-after verification. Currency refresh is explicitly requested after Cash or Marked Coin changes instead of treating a ProcessEvent dispatch as proof of success.");
            ImGui::Spacing();
            ImGui::TextWrapped("While this menu is open, Win32 mouse/keyboard/raw-input messages are consumed by the overlay, the cursor is software-rendered in front of the game, and the local UE controller/pawn is temporarily input-blocked. The block is balanced and restored when the menu closes.");
            break;
        }
    }
}

void Menu::ReleaseInputPriority()
{
    if (g_inputBlockedPawn && g_inputBlockedController)
        SetPawnInputEnabled(g_inputBlockedPawn, g_inputBlockedController, true);

    if (g_inputBlockedController)
        SetControllerIgnored(g_inputBlockedController, false);

    g_inputBlockedPawn = 0;
    g_inputBlockedController = 0;
}

void Menu::MaintainInputPriority()
{
    if (!bOpen)
    {
        ReleaseInputPriority();
        return;
    }

    const uintptr_t controller = GameAccess::GetLocalController();
    const uintptr_t pawn = GameAccess::GetLocalPawn();
    if (!controller)
        return;

    if (controller == g_inputBlockedController && pawn == g_inputBlockedPawn)
        return;

    ReleaseInputPriority();
    SetControllerIgnored(controller, true);

    if (pawn)
        SetPawnInputEnabled(pawn, controller, false);

    GameAccess::InvokeFunctionRaw(controller, FunctionIndices::Controller_StopMovement,
        nullptr, 0);
    g_inputBlockedController = controller;
    g_inputBlockedPawn = pawn;
}

void Menu::SetOpen(bool open)
{
    if (bOpen == open)
        return;
    bOpen = open;
    if (!bOpen)
        ReleaseInputPriority();
}

void Menu::Toggle()
{
    SetOpen(!bOpen);
}

void Menu::Render()
{
    if (!bOpen)
        return;

    ApplyProfessionalStyle();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport)
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(980.0f, 700.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(860.0f, 600.0f), ImVec2(1400.0f, 1000.0f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##IRRControlCenter", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    const bool hookReady = IsHookInstalled();
    const bool queueReady = HasCapturedCommandQueue();
    const bool rendererReady = IsRendererInitialized();
    const bool worldReady = GameAccess::GetWorld() != 0;
    const bool pawnReady = GameAccess::GetLocalPawn() != 0;

    // Keep a complete text line below the runtime-status row. The previous
    // 104 px child placed this row too close to the child clip rectangle once
    // WindowPadding, the child border and display scaling were applied.
    constexpr float headerHeight = 120.0f;
    constexpr float statusRowY = 76.0f;
    ImGui::BeginChild("##TopHeader", ImVec2(0.0f, headerHeight), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPosY(8.0f);
    ImGui::SetWindowFontScale(1.22f);
    ImGui::TextColored(ImVec4(0.27f, 0.76f, 1.00f, 1.00f), "IRR  CONTROL CENTER");
    ImGui::SetWindowFontScale(1.0f);

    const float closeX = ImGui::GetWindowContentRegionMax().x - 76.0f;
    ImGui::SameLine(closeX);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.34f, 0.10f, 0.13f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.14f, 0.18f, 1.00f));
    if (ImGui::Button("CLOSE", ImVec2(70.0f, 28.0f)))
        SetOpen(false);
    ImGui::PopStyleColor(2);

    ImGui::SetCursorPosY(40.0f);
    ImGui::TextDisabled("Incursion: Red River  |  optimized internal toolkit");
    ImGui::SetCursorPosY(statusRowY);
    StatusValue("HOOK", hookReady); ImGui::SameLine(0.0f, 16.0f);
    StatusValue("QUEUE", queueReady); ImGui::SameLine(0.0f, 16.0f);
    StatusValue("DX12", rendererReady); ImGui::SameLine(0.0f, 16.0f);
    StatusValue("WORLD", worldReady); ImGui::SameLine(0.0f, 16.0f);
    StatusValue("PAWN", pawnReady);
    ImGui::EndChild();

    ImGui::Spacing();
    const float sidebarWidth = 194.0f;
    ImGui::BeginChild("##Sidebar", ImVec2(sidebarWidth, -30.0f), true);
    ImGui::TextColored(ImVec4(0.27f, 0.76f, 1.00f, 1.00f), "NAVIGATION");
    ImGui::Separator();
    NavGroup("COMBAT");
    NavItem("  ESP & Visuals", MenuPage::ESP);
    NavItem("  Aim Assist", MenuPage::Aimbot);
    NavItem("  Tactical", MenuPage::Tactical);
    NavGroup("PLAYER");
    NavItem("  Player", MenuPage::Player);
    NavItem("  Movement+", MenuPage::Movement);
    NavGroup("WORLD / ITEMS");
    NavItem("  World & Raid", MenuPage::World);
    NavItem("  Profile & Economy", MenuPage::Profile);
    NavItem("  Loot Tools", MenuPage::Items);
    NavItem("  Item Spawner", MenuPage::Spawner);
    NavGroup("SYSTEM");
    NavItem("  Diagnostics", MenuPage::Diagnostics);
    NavItem("  Build Notes", MenuPage::Notes);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##Workspace", ImVec2(0.0f, -30.0f), true);
    ImGui::SetWindowFontScale(1.12f);
    ImGui::Text("%s", PageTitle(g_page));
    ImGui::SetWindowFontScale(1.0f);
    if (g_page == MenuPage::Diagnostics)
    {
        const float buttonWidth = 200.0f;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - buttonWidth);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.42f, 0.68f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.54f, 0.82f, 1.00f));
        if (ImGui::Button("WRITE DIAGNOSTIC DUMP", ImVec2(buttonWidth, 26.0f)))
            WriteDiagnosticDump();
        ImGui::PopStyleColor(2);
    }
    ImGui::TextDisabled("%s", PageSubtitle(g_page));
    ImGui::Separator();
    ImGui::Spacing();
    RenderCurrentPage();
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextDisabled("HOME / INSERT / DELETE  |  MENU INPUT LOCK: ACTIVE  |  EXE 0x%llX",
        static_cast<unsigned long long>(Memory::GetBase()));

    ImGui::End();
}
