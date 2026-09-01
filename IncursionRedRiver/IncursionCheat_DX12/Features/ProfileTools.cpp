#include "ProfileTools.h"

#include "InventoryService.h"
#include "ItemCatalog.h"
#include "../hooks/PresentHook.h"
#include "../sdk/GameAccess.h"
#include "../sdk/Offsets.h"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace
{
    struct ItemChoice
    {
        const char* Label;
        int32_t PackageIndex;
    };

    constexpr std::array<ItemChoice, 4> ItemChoices = {{
        { "Cash / money", ObjectIndices::Package_ID_Cash },
        { "Marked Coin / token", ObjectIndices::Package_ID_MarkedCoin },
        { "Bandage", ObjectIndices::Package_ID_Bandage },
        { "Health Injector", ObjectIndices::Package_ID_HealthInjector }
    }};

    int g_selectedItem = 0;
    int g_itemAmount = 1000;
    int g_itemDestination = 2; // Auto: verify stash first, then player inventory.
    int g_level = 10;
    float g_xp = 0.0f;
    float g_overallReputation = 100.0f;
    float g_factionReputation = 100.0f;
    char g_factionName[96]{};
    char g_lastOperation[256] = "No profile operation submitted yet.";
    InventoryService::Result g_lastInventoryResult{};
    std::atomic<bool> g_profileInventoryPending{ false };
    std::mutex g_profileResultMutex;

    void SetStatus(const char* operation, bool dispatched)
    {
        std::lock_guard<std::mutex> lock(g_profileResultMutex);
        std::snprintf(g_lastOperation, sizeof(g_lastOperation), "%s: %s",
            operation, dispatched ? "ProcessEvent dispatched" : "blocked / validation failed");
    }

    uintptr_t GetValidatedGameInstance()
    {
        const auto& diagnostics = GameAccess::GetDiagnostics();
        return diagnostics.GameInstanceTypeValid ? GameAccess::GetGameInstance() : 0;
    }

    const ItemCatalog::Entry* FindCatalogEntry(int32_t packageIndex)
    {
        const auto it = std::find_if(ItemCatalog::Entries.begin(), ItemCatalog::Entries.end(),
            [packageIndex](const ItemCatalog::Entry& e) { return e.PackageIndex == packageIndex; });
        return it == ItemCatalog::Entries.end() ? nullptr : &*it;
    }

    bool AddProfileItem(int32_t packageIndex, int amount, const char* label)
    {
        const unsigned long gameThread = GetGameWindowThreadId();
        if (!gameThread || GetCurrentThreadId() != gameThread)
        {
            if (g_profileInventoryPending.exchange(true))
                return false;
            const std::string labelCopy = label ? label : "Profile item";
            {
                std::lock_guard<std::mutex> lock(g_profileResultMutex);
                std::snprintf(g_lastOperation, sizeof(g_lastOperation),
                    "QUEUED: %s x%d on Unreal game thread", labelCopy.c_str(), amount);
            }
            const bool queued = QueueGameThreadTask([packageIndex, amount, labelCopy]()
            {
                AddProfileItem(packageIndex, amount, labelCopy.c_str());
                g_profileInventoryPending.store(false);
            });
            if (!queued)
            {
                std::lock_guard<std::mutex> lock(g_profileResultMutex);
                std::snprintf(g_lastOperation, sizeof(g_lastOperation),
                    "%s: failed to queue on Unreal game thread", labelCopy.c_str());
                g_profileInventoryPending.store(false);
            }
            return queued;
        }

        std::lock_guard<std::mutex> resultLock(g_profileResultMutex);
        const ItemCatalog::Entry* entry = FindCatalogEntry(packageIndex);
        if (!entry)
        {
            std::snprintf(g_lastOperation, sizeof(g_lastOperation), "%s: catalog entry missing", label);
            return false;
        }

        if (g_itemDestination == 2)
        {
            InventoryService::Result stashResult{};
            if (InventoryService::AddItem(*entry, amount, InventoryService::Destination::Stash, &stashResult))
            {
                g_lastInventoryResult = stashResult;
                std::snprintf(g_lastOperation, sizeof(g_lastOperation), "AUTO -> STASH | %s", stashResult.Message);
                return true;
            }

            InventoryService::Result playerResult{};
            const bool playerOk = InventoryService::AddItem(*entry, amount,
                InventoryService::Destination::PlayerInventory, &playerResult);
            g_lastInventoryResult = playerResult;
            std::snprintf(g_lastOperation, sizeof(g_lastOperation), "AUTO -> %s | %s",
                playerOk ? "INVENTORY" : "NO VERIFIED DESTINATION", playerResult.Message);
            return playerOk;
        }

        const auto destination = g_itemDestination == 0 ?
            InventoryService::Destination::PlayerInventory : InventoryService::Destination::Stash;
        const bool ok = InventoryService::AddItem(*entry, amount, destination, &g_lastInventoryResult);
        std::snprintf(g_lastOperation, sizeof(g_lastOperation), "%s", g_lastInventoryResult.Message);
        return ok;
    }

    void QueueCurrencyRefresh(uintptr_t playerInventory, uintptr_t stashInventory)
    {
        if (g_profileInventoryPending.exchange(true))
            return;
        {
            std::lock_guard<std::mutex> lock(g_profileResultMutex);
            std::snprintf(g_lastOperation, sizeof(g_lastOperation),
                "QUEUED: currency refresh on Unreal game thread");
        }
        const bool queued = QueueGameThreadTask([playerInventory, stashInventory]()
        {
            InventoryService::RefreshInventory(playerInventory);
            InventoryService::RefreshInventory(stashInventory);
            {
                std::lock_guard<std::mutex> lock(g_profileResultMutex);
                std::snprintf(g_lastOperation, sizeof(g_lastOperation),
                    "Currency refresh submitted to inventory + stash.");
            }
            g_profileInventoryPending.store(false);
        });
        if (!queued)
        {
            std::lock_guard<std::mutex> lock(g_profileResultMutex);
            std::snprintf(g_lastOperation, sizeof(g_lastOperation),
                "Currency refresh: failed to queue on Unreal game thread");
            g_profileInventoryPending.store(false);
        }
    }

    std::wstring ToWide(const char* input)
    {
        if (!input || !*input)
            return {};
        const int required = MultiByteToWideChar(CP_UTF8, 0, input, -1, nullptr, 0);
        if (required <= 1)
            return {};
        std::wstring output(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, input, -1, output.data(), required);
        output.resize(static_cast<size_t>(required - 1));
        return output;
    }

    bool SubmitFactionReputation()
    {
        const uintptr_t gameInstance = GetValidatedGameInstance();
        std::wstring faction = ToWide(g_factionName);
        if (!gameInstance || faction.empty())
            return false;
        struct FStringParam
        {
            wchar_t* Data = nullptr;
            int32_t Count = 0;
            int32_t Capacity = 0;
        };
        struct alignas(8) Params
        {
            float Reputation = 0.0f;
            uint32_t Padding = 0;
            FStringParam Faction{};
        } params{};
        params.Reputation = g_factionReputation;
        params.Faction.Data = faction.data();
        params.Faction.Count = static_cast<int32_t>(faction.size() + 1);
        params.Faction.Capacity = params.Faction.Count;
        return GameAccess::InvokeFunctionRaw(gameInstance,
            FunctionIndices::GeneralGameInstance_SetFactionReputation,
            &params, sizeof(params));
    }
}

namespace ProfileTools
{
    void RenderTab()
    {
        const uintptr_t gameInstance = GetValidatedGameInstance();
        const uintptr_t playerInventory = InventoryService::GetPlayerInventory();
        const uintptr_t stashInventory = InventoryService::GetStashInventory();

        ImGui::TextUnformatted("PROFILE & ECONOMY");
        ImGui::TextDisabled("Save-backed profile operations and verified inventory currency insertion.");
        ImGui::Spacing();
        ImGui::Text("GameInstance  0x%llX", static_cast<unsigned long long>(gameInstance));
        ImGui::Text("Inventory     0x%llX", static_cast<unsigned long long>(playerInventory));
        ImGui::Text("Stash         0x%llX", static_cast<unsigned long long>(stashInventory));

        ImGui::Separator();
        ImGui::TextUnformatted("CURRENCY / PROFILE ITEMS");
        ImGui::TextWrapped("Cash and Marked Coin are real IRRItemDefinition assets. They are inserted through InventoryComponent and UpdateCurrency is called immediately afterward instead of relying on the old GeneralGameInstance.AddItem wrapper.");

        const char* destinationLabel = g_itemDestination == 0 ? "Player inventory" :
            (g_itemDestination == 1 ? "Hideout stash" : "Auto-detect (stash -> inventory)");
        if (ImGui::BeginCombo("Destination", destinationLabel))
        {
            if (ImGui::Selectable("Player inventory", g_itemDestination == 0)) g_itemDestination = 0;
            if (ImGui::Selectable("Hideout stash", g_itemDestination == 1)) g_itemDestination = 1;
            if (ImGui::Selectable("Auto-detect (stash -> inventory)", g_itemDestination == 2)) g_itemDestination = 2;
            ImGui::EndCombo();
        }
        ImGui::InputInt("Amount", &g_itemAmount, 1, 100);
        g_itemAmount = std::clamp(g_itemAmount, 1, 1000000);

        if (ImGui::Button("ADD CASH", ImVec2(145.0f, 34.0f)))
            AddProfileItem(ObjectIndices::Package_ID_Cash, g_itemAmount, "Cash");
        ImGui::SameLine();
        if (ImGui::Button("ADD MARKED COINS", ImVec2(165.0f, 34.0f)))
            AddProfileItem(ObjectIndices::Package_ID_MarkedCoin, g_itemAmount, "Marked Coin");
        ImGui::SameLine();
        if (ImGui::Button("REFRESH CURRENCY", ImVec2(160.0f, 34.0f)))
            QueueCurrencyRefresh(playerInventory, stashInventory);

        ImGui::Spacing();
        if (ImGui::BeginCombo("Profile item", ItemChoices[static_cast<size_t>(g_selectedItem)].Label))
        {
            for (int i = 0; i < static_cast<int>(ItemChoices.size()); ++i)
            {
                const bool selected = i == g_selectedItem;
                if (ImGui::Selectable(ItemChoices[static_cast<size_t>(i)].Label, selected))
                    g_selectedItem = i;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Add selected profile item"))
        {
            const auto& choice = ItemChoices[static_cast<size_t>(g_selectedItem)];
            AddProfileItem(choice.PackageIndex, g_itemAmount, choice.Label);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("PLAYER LEVEL");
        ImGui::InputInt("Level", &g_level, 1, 10);
        ImGui::InputFloat("XP", &g_xp, 100.0f, 1000.0f, "%.1f");
        g_level = std::clamp(g_level, 1, 1000);
        if (ImGui::Button("Set level and XP"))
        {
            struct Params { int32_t Level; float XP; } params{ g_level, std::max(g_xp, 0.0f) };
            SetStatus("GeneralGameInstance.SetLevel", gameInstance &&
                GameAccess::InvokeFunctionRaw(gameInstance,
                    FunctionIndices::GeneralGameInstance_SetLevel,
                    &params, sizeof(params)));
        }

        ImGui::Separator();
        ImGui::TextUnformatted("FACTION REPUTATION");
        ImGui::InputFloat("Overall reputation", &g_overallReputation, 1.0f, 10.0f, "%.1f");
        if (ImGui::Button("Set overall faction reputation"))
        {
            float params = g_overallReputation;
            SetStatus("GeneralGameInstance.SetOverallFactionReputation",
                gameInstance && GameAccess::InvokeFunctionRaw(gameInstance,
                    FunctionIndices::GeneralGameInstance_SetOverallFactionReputation,
                    &params, sizeof(params)));
        }

        ImGui::InputText("Exact faction name", g_factionName, sizeof(g_factionName));
        ImGui::InputFloat("Named faction reputation", &g_factionReputation, 1.0f, 10.0f, "%.1f");
        if (ImGui::Button("Set named faction reputation"))
            SetStatus("GeneralGameInstance.SetFactionReputation", SubmitFactionReputation());

        ImGui::Separator();
        InventoryService::Result displayInventoryResult{};
        std::array<char, 256> displayOperation{};
        {
            std::lock_guard<std::mutex> lock(g_profileResultMutex);
            displayInventoryResult = g_lastInventoryResult;
            std::snprintf(displayOperation.data(), displayOperation.size(), "%s", g_lastOperation);
        }
        const ImVec4 statusColor = displayInventoryResult.Success ?
            ImVec4(0.35f, 0.95f, 0.62f, 1.0f) : ImVec4(0.90f, 0.72f, 0.35f, 1.0f);
        ImGui::TextColored(statusColor, "%s", displayOperation.data());
        if (displayInventoryResult.TargetComponent)
        {
            ImGui::TextDisabled("Inventory verification: %lld -> %lld | backend: %s | container: %d",
                static_cast<long long>(displayInventoryResult.CountBefore),
                static_cast<long long>(displayInventoryResult.CountAfter),
                displayInventoryResult.Backend, displayInventoryResult.ContainerIndex);
        }
        ImGui::TextDisabled("Profile changes may be save-backed. Test a small value first, then make a normal game save after verifying it.");
    }
}
