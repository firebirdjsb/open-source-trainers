#include "Spawner.h"

#include "InventoryService.h"
#include "ItemCatalog.h"
#include "../hooks/PresentHook.h"
#include "../sdk/GameAccess.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    enum class Category
    {
        All = 0,
        CompleteWeapons,
        GunParts,
        AmmoMags,
        Medical,
        ArmorGear,
        OpticsAccessories,
        ResourcesUtility,
        Count
    };

    constexpr std::array<const char*, static_cast<size_t>(Category::Count)> CategoryNames = {{
        "All", "Complete weapons", "Gun parts", "Ammo / magazines", "Medical",
        "Armor / gear", "Optics / accessories", "Resources / utility"
    }};

    constexpr std::array<const char*, 2> DestinationNames = {{
        "Player inventory", "Hideout stash"
    }};

    char g_filter[96]{};
    int g_amount = 1;
    int g_selected = 0;
    int g_category = 0;
    int g_destination = 0;
    InventoryService::Result g_lastResult{};
    char g_status[256] = "Ready. Direct inventory/stash insertion uses the game's InventoryComponent backend.";
    std::atomic<bool> g_operationPending{ false };
    std::mutex g_resultMutex;

    void StoreResult(const InventoryService::Result& result, const char* status)
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_lastResult = result;
        std::snprintf(g_status, sizeof(g_status), "%s", status ? status : result.Message);
    }

    void SnapshotResult(InventoryService::Result& result,
                        std::array<char, 256>& status)
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        result = g_lastResult;
        std::snprintf(status.data(), status.size(), "%s", g_status);
    }

    bool ContainsInsensitive(const char* text, const char* needle)
    {
        if (!needle || !*needle)
            return true;
        if (!text)
            return false;
        std::string hay(text), ndl(needle);
        std::transform(hay.begin(), hay.end(), hay.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(ndl.begin(), ndl.end(), ndl.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return hay.find(ndl) != std::string::npos;
    }

    bool HasAny(const char* id, std::initializer_list<const char*> needles)
    {
        for (const char* needle : needles)
            if (std::strstr(id, needle))
                return true;
        return false;
    }

    Category Classify(const ItemCatalog::Entry& entry)
    {
        const char* id = entry.Id;
        if (InventoryService::IsCompleteWeapon(entry) ||
            std::strcmp(id, "ID_Melee_Temporary") == 0)
            return Category::CompleteWeapons;
        if (HasAny(id, { "Bandage", "Injector" }))
            return Category::Medical;
        if (HasAny(id, { "Armor", "Helmet", "Rig", "Plate_Carrier", "Back", "Night_Vision", "Goggles" }))
            return Category::ArmorGear;
        if (HasAny(id, { "x19_", "x39_", "x45_", "x51_", "x300_", "x338_", "12x70_", "rnd", "Rnd", "Drum", "Stanag", "Powermag" }))
            return Category::AmmoMags;
        if (HasAny(id, { "Foregrip", "Suppressor", "Muzzle", "Barrel", "Handguard",
                         "PistolGrip", "Pistol_Grip", "Stock", "Dust_Cover", "Gas_Tube",
                         "Dovetail", "Slide", "Buttstock", "Buffertube", "Shroud", "Chassis",
                         "Front_Sight_With_Gas_Block" }))
            return Category::GunParts;
        if (HasAny(id, { "Scope", "Sight", "Flash", "Thermal", "Compass" }))
            return Category::OpticsAccessories;
        return Category::ResourcesUtility;
    }

    InventoryService::Destination Destination()
    {
        return g_destination == 1 ?
            InventoryService::Destination::Stash :
            InventoryService::Destination::PlayerInventory;
    }

    bool SubmitItem(const ItemCatalog::Entry& entry, int amount)
    {
        if (g_operationPending.exchange(true))
            return false;

        const InventoryService::Destination destination = Destination();
        const int requestedAmount = amount;
        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            std::snprintf(g_status, sizeof(g_status),
                "QUEUED: %s x%d -> %s on Unreal game thread",
                entry.Id, requestedAmount,
                destination == InventoryService::Destination::Stash ? "stash" : "inventory");
        }

        const bool queued = QueueGameThreadTask([entry, requestedAmount, destination]()
        {
            InventoryService::Result result{};
            InventoryService::AddItem(entry, requestedAmount, destination, &result);
            StoreResult(result, result.Message);
            DebugLog("[Spawner] Completed %s on thread %lu (window thread %lu).\n",
                entry.Id, GetLastGameTaskThreadId(),
                GetGameWindowThreadId());
            g_operationPending.store(false);
        });
        if (!queued)
        {
            InventoryService::Result result{};
            std::snprintf(result.Message, sizeof(result.Message),
                "%s: failed to queue operation on Unreal game thread", entry.Id);
            StoreResult(result, result.Message);
            g_operationPending.store(false);
        }
        return queued;
    }

    void AddPreset(std::initializer_list<std::pair<int32_t, int>> preset)
    {
        if (g_operationPending.exchange(true))
            return;
        const std::vector<std::pair<int32_t, int>> requested(preset);
        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            std::snprintf(g_status, sizeof(g_status),
                "QUEUED: quick kit (%zu entries) on Unreal game thread", requested.size());
        }
        const bool queued = QueueGameThreadTask([requested]()
        {
            int success = 0;
            int attempted = 0;
            InventoryService::Result last{};
            for (const auto& wanted : requested)
            {
                const auto it = std::find_if(ItemCatalog::Entries.begin(), ItemCatalog::Entries.end(),
                    [&](const ItemCatalog::Entry& e) { return e.PackageIndex == wanted.first; });
                if (it == ItemCatalog::Entries.end())
                    continue;
                ++attempted;
                success += InventoryService::AddItem(*it, wanted.second,
                    InventoryService::Destination::PlayerInventory, &last) ? 1 : 0;
            }
            char status[256]{};
            std::snprintf(status, sizeof(status),
                "Quick kit -> player inventory: %d/%d verified item insertions",
                success, attempted);
            StoreResult(last, status);
            g_operationPending.store(false);
        });
        if (!queued)
        {
            InventoryService::Result result{};
            std::snprintf(result.Message, sizeof(result.Message),
                "Quick kit: failed to queue operation on Unreal game thread");
            StoreResult(result, result.Message);
            g_operationPending.store(false);
        }
    }
}

namespace Spawner
{
    void RenderTab()
    {
        const bool operationPending = g_operationPending.load();
        const uintptr_t playerInventory = operationPending ? 0 : InventoryService::GetPlayerInventory();
        const uintptr_t stashInventory = operationPending ? 0 : InventoryService::GetStashInventory();

        ImGui::TextUnformatted("ITEM DELIVERY");
        ImGui::TextDisabled("Game-owned inventory insertion with before/after verification.");
        ImGui::TextDisabled("Dispatch: window/game thread %lu | last task thread %lu | %s",
            GetGameWindowThreadId(), GetLastGameTaskThreadId(),
            operationPending ? "MUTATION PENDING" : "IDLE");
        ImGui::Spacing();

        ImGui::Text("Player inventory: 0x%llX", static_cast<unsigned long long>(playerInventory));
        ImGui::SameLine();
        ImGui::TextColored(playerInventory ? ImVec4(0.35f, 0.95f, 0.62f, 1.0f) : ImVec4(1.0f, 0.45f, 0.38f, 1.0f),
            playerInventory ? "  READY" : "  WAIT");
        ImGui::Text("Hideout stash:   0x%llX", static_cast<unsigned long long>(stashInventory));
        ImGui::SameLine();
        ImGui::TextColored(stashInventory ? ImVec4(0.35f, 0.95f, 0.62f, 1.0f) : ImVec4(1.0f, 0.66f, 0.25f, 1.0f),
            stashInventory ? "  READY" : "  NOT RESOLVED");

        const auto playerProbe = operationPending ? InventoryService::InventoryProbe{} :
            InventoryService::ProbeInventory(playerInventory);
        const auto stashProbe = operationPending ? InventoryService::InventoryProbe{} :
            InventoryService::ProbeInventory(stashInventory);
        ImGui::TextDisabled("Inventory containers: %d/%d | item records: %d",
            playerProbe.MainContainerCount, playerProbe.MainContainerCapacity,
            playerProbe.TotalContainerItems);
        ImGui::TextDisabled("Stash containers: %d/%d | item records: %d",
            stashProbe.MainContainerCount, stashProbe.MainContainerCapacity,
            stashProbe.TotalContainerItems);

        ImGui::Separator();
        ImGui::InputTextWithHint("##itemsearch", "Search 113 current-build item IDs...", g_filter, sizeof(g_filter));

        if (ImGui::BeginCombo("Category", CategoryNames[static_cast<size_t>(g_category)]))
        {
            for (int i = 0; i < static_cast<int>(Category::Count); ++i)
            {
                const bool selected = g_category == i;
                if (ImGui::Selectable(CategoryNames[static_cast<size_t>(i)], selected))
                    g_category = i;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Destination", DestinationNames[static_cast<size_t>(g_destination)]))
        {
            for (int i = 0; i < static_cast<int>(DestinationNames.size()); ++i)
            {
                const bool selected = g_destination == i;
                if (ImGui::Selectable(DestinationNames[static_cast<size_t>(i)], selected))
                    g_destination = i;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::InputInt("Amount", &g_amount, 1, 25);
        g_amount = std::clamp(g_amount, 1, 1000000);

        if (g_selected < 0 || g_selected >= static_cast<int>(ItemCatalog::Entries.size()))
            g_selected = 0;

        if (ImGui::BeginChild("##itemcatalog", ImVec2(0.0f, 315.0f), true))
        {
            for (int i = 0; i < static_cast<int>(ItemCatalog::Entries.size()); ++i)
            {
                const auto& entry = ItemCatalog::Entries[static_cast<size_t>(i)];
                if (g_category != static_cast<int>(Category::All) &&
                    Classify(entry) != static_cast<Category>(g_category))
                    continue;
                if (!ContainsInsensitive(entry.Id, g_filter))
                    continue;
                if (ImGui::Selectable(entry.Id, g_selected == i))
                    g_selected = i;
            }
        }
        ImGui::EndChild();

        const auto& selected = ItemCatalog::Entries[static_cast<size_t>(g_selected)];
        ImGui::Text("Selected: %s", selected.Id);
        ImGui::TextDisabled("Package 0x%X | live IRRItemDefinition 0x%X",
            selected.PackageIndex, selected.DefinitionIndex);
        if (InventoryService::IsCompleteWeapon(selected))
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.62f, 1.0f),
                "COMPLETE WEAPON: preset root + default vital parts");
        else if (Classify(selected) == Category::GunParts)
            ImGui::TextDisabled("GUN PART: standalone definition (no complete-weapon preset)");

        const char* buttonLabel = g_destination == 0 ? "ADD TO INVENTORY" : "ADD TO STASH";
        if (ImGui::Button(buttonLabel, ImVec2(190.0f, 34.0f)) && !g_operationPending.load())
            SubmitItem(selected, g_amount);
        ImGui::SameLine();
        if (ImGui::Button("ADD x10", ImVec2(110.0f, 34.0f)) && !g_operationPending.load())
            SubmitItem(selected, 10);

        ImGui::Separator();
        ImGui::TextUnformatted("QUICK KITS");
        ImGui::TextDisabled("Kits always target player inventory and verify each insertion.");
        if (ImGui::Button("Medical kit") && !g_operationPending.load())
            AddPreset({ { 0xC653, 10 }, { 0xC85C, 10 } });
        ImGui::SameLine();
        if (ImGui::Button("M4 starter kit") && !g_operationPending.load())
            AddPreset({ { 0xCB91, 1 }, { 0xD014, 8 }, { 0xC866, 240 } });
        ImGui::SameLine();
        if (ImGui::Button("AK starter kit") && !g_operationPending.load())
            AddPreset({ { 0xC8FC, 1 }, { 0xC8F5, 8 }, { 0xC85B, 240 } });

        ImGui::Spacing();
        InventoryService::Result displayResult{};
        std::array<char, 256> displayStatus{};
        SnapshotResult(displayResult, displayStatus);
        const ImVec4 statusColor = displayResult.Success ?
            ImVec4(0.35f, 0.95f, 0.62f, 1.0f) : ImVec4(0.90f, 0.72f, 0.35f, 1.0f);
        ImGui::TextColored(statusColor, "%s", displayStatus.data());
        if (displayResult.TargetComponent)
        {
            ImGui::TextDisabled("Backend: %s | component: 0x%llX | container: %d | observed count: %lld -> %lld",
                displayResult.Backend,
                static_cast<unsigned long long>(displayResult.TargetComponent),
                displayResult.ContainerIndex,
                static_cast<long long>(displayResult.CountBefore),
                static_cast<long long>(displayResult.CountAfter));
            ImGui::TextDisabled("MainContainers: %d | DefaultItem type: %d | dispatches: %d",
                displayResult.MainContainerCount, displayResult.DefaultItemType,
                displayResult.DispatchCount);
            ImGui::TextDisabled("Returned definition: 0x%llX | CanAdd: %s | TryAdd return: %s",
                static_cast<unsigned long long>(displayResult.ReturnedDefinition),
                displayResult.CanAddBuiltItem ? "YES" : "NO",
                displayResult.TryAddReturned ? "YES" : "NO");
            ImGui::TextDisabled("Preset: 0x%llX | complete: %s | attachments: %d | records: %d -> %d",
                static_cast<unsigned long long>(displayResult.PresetObject),
                displayResult.CompleteWeapon ? "YES" : "NO",
                displayResult.ExpectedAttachments,
                displayResult.ItemRecordsBefore, displayResult.ItemRecordsAfter);
        }
    }

    void Render() { RenderTab(); }
}
