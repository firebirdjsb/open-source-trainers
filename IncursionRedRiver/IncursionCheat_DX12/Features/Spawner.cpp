#include "Spawner.h"

#include "InventoryService.h"
#include "ItemCatalog.h"
#include "../sdk/GameAccess.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    enum class Category
    {
        All = 0,
        Weapons,
        AmmoMags,
        Medical,
        ArmorGear,
        Attachments,
        ResourcesUtility,
        Count
    };

    constexpr std::array<const char*, static_cast<size_t>(Category::Count)> CategoryNames = {{
        "All", "Weapons", "Ammo / magazines", "Medical", "Armor / gear",
        "Attachments / optics", "Resources / utility"
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

    Category Classify(const char* id)
    {
        if (HasAny(id, { "Bandage", "Injector" }))
            return Category::Medical;
        if (HasAny(id, { "Armor", "Helmet", "Rig", "Plate_Carrier", "Back", "Night_Vision", "Goggles" }))
            return Category::ArmorGear;
        if (HasAny(id, { "x19_", "x39_", "x45_", "x51_", "x300_", "x338_", "12x70_", "rnd", "Rnd", "Drum", "Stanag", "Powermag" }))
            return Category::AmmoMags;
        if (HasAny(id, { "Scope", "Sight", "Flash", "Foregrip", "Suppressor", "Muzzle", "Barrel", "Handguard", "PistolGrip", "Pistol_Grip", "Stock", "Dust_Cover", "Gas_Tube", "Dovetail", "Slide", "Buttstock", "Buffertube", "Shroud" }))
            return Category::Attachments;
        if (HasAny(id, { "AK-", "AKS-", "M700", "Glock_19", "QSZ-92", "Simonov_SKS", "MP5SD", "Colt_M4A1", "KRISS_Vector_45ACP", "RIA_VR80", "Melee" }))
            return Category::Weapons;
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
        const bool ok = InventoryService::AddItem(entry, amount, Destination(), &g_lastResult);
        std::snprintf(g_status, sizeof(g_status), "%s", g_lastResult.Message);
        return ok;
    }

    void AddPreset(std::initializer_list<std::pair<int32_t, int>> preset)
    {
        int success = 0;
        int attempted = 0;
        InventoryService::Result last{};
        for (const auto& wanted : preset)
        {
            const auto it = std::find_if(ItemCatalog::Entries.begin(), ItemCatalog::Entries.end(),
                [&](const ItemCatalog::Entry& e) { return e.PackageIndex == wanted.first; });
            if (it == ItemCatalog::Entries.end())
                continue;
            ++attempted;
            success += InventoryService::AddItem(*it, wanted.second,
                InventoryService::Destination::PlayerInventory, &last) ? 1 : 0;
        }
        g_lastResult = last;
        std::snprintf(g_status, sizeof(g_status),
            "Quick kit -> player inventory: %d/%d verified item insertions", success, attempted);
    }
}

namespace Spawner
{
    void RenderTab()
    {
        const uintptr_t playerInventory = InventoryService::GetPlayerInventory();
        const uintptr_t stashInventory = InventoryService::GetStashInventory();

        ImGui::TextUnformatted("ITEM DELIVERY");
        ImGui::TextDisabled("Game-owned inventory insertion with before/after verification.");
        ImGui::Spacing();

        ImGui::Text("Player inventory: 0x%llX", static_cast<unsigned long long>(playerInventory));
        ImGui::SameLine();
        ImGui::TextColored(playerInventory ? ImVec4(0.35f, 0.95f, 0.62f, 1.0f) : ImVec4(1.0f, 0.45f, 0.38f, 1.0f),
            playerInventory ? "  READY" : "  WAIT");
        ImGui::Text("Hideout stash:   0x%llX", static_cast<unsigned long long>(stashInventory));
        ImGui::SameLine();
        ImGui::TextColored(stashInventory ? ImVec4(0.35f, 0.95f, 0.62f, 1.0f) : ImVec4(1.0f, 0.66f, 0.25f, 1.0f),
            stashInventory ? "  READY" : "  NOT RESOLVED");

        const auto playerProbe = InventoryService::ProbeInventory(playerInventory);
        const auto stashProbe = InventoryService::ProbeInventory(stashInventory);
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
                    Classify(entry.Id) != static_cast<Category>(g_category))
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

        const char* buttonLabel = g_destination == 0 ? "ADD TO INVENTORY" : "ADD TO STASH";
        if (ImGui::Button(buttonLabel, ImVec2(190.0f, 34.0f)))
            SubmitItem(selected, g_amount);
        ImGui::SameLine();
        if (ImGui::Button("ADD x10", ImVec2(110.0f, 34.0f)))
            SubmitItem(selected, 10);

        ImGui::Separator();
        ImGui::TextUnformatted("QUICK KITS");
        ImGui::TextDisabled("Kits always target player inventory and verify each insertion.");
        if (ImGui::Button("Medical kit"))
            AddPreset({ { 0xC653, 10 }, { 0xC85C, 10 } });
        ImGui::SameLine();
        if (ImGui::Button("M4 starter kit"))
            AddPreset({ { 0xCB91, 1 }, { 0xD014, 8 }, { 0xC866, 240 } });
        ImGui::SameLine();
        if (ImGui::Button("AK starter kit"))
            AddPreset({ { 0xC8FC, 1 }, { 0xC8F5, 8 }, { 0xC85B, 240 } });

        ImGui::Spacing();
        const ImVec4 statusColor = g_lastResult.Success ?
            ImVec4(0.35f, 0.95f, 0.62f, 1.0f) : ImVec4(0.90f, 0.72f, 0.35f, 1.0f);
        ImGui::TextColored(statusColor, "%s", g_status);
        if (g_lastResult.TargetComponent)
        {
            ImGui::TextDisabled("Backend: %s | component: 0x%llX | container: %d | observed count: %lld -> %lld",
                g_lastResult.Backend,
                static_cast<unsigned long long>(g_lastResult.TargetComponent),
                g_lastResult.ContainerIndex,
                static_cast<long long>(g_lastResult.CountBefore),
                static_cast<long long>(g_lastResult.CountAfter));
            ImGui::TextDisabled("MainContainers: %d | DefaultItem type: %d | dispatches: %d",
                g_lastResult.MainContainerCount, g_lastResult.DefaultItemType,
                g_lastResult.DispatchCount);
            ImGui::TextDisabled("Returned definition: 0x%llX | CanAdd: %s | TryAdd return: %s",
                static_cast<unsigned long long>(g_lastResult.ReturnedDefinition),
                g_lastResult.CanAddBuiltItem ? "YES" : "NO",
                g_lastResult.TryAddReturned ? "YES" : "NO");
        }
    }

    void Render() { RenderTab(); }
}
