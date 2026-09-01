#include "InventoryService.h"

#include "ItemCatalog.h"
#include "../Memory/Memory.h"
#include "../sdk/GameAccess.h"
#include "../sdk/Offsets.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    struct TArrayHeader
    {
        uintptr_t Data = 0;
        int32_t Count = 0;
        int32_t Capacity = 0;
    };

    constexpr size_t MainContainerStride = 0x38;
    constexpr size_t MainContainer_Items = 0x28;
    constexpr size_t ContainerItemStride = 0xF8;
    constexpr size_t ContainerItem_Definition = 0x10;
    constexpr size_t ContainerItem_Count = 0xE8;
    constexpr size_t DefaultItemStride = 0x1E8;
    constexpr size_t DefaultItem_ItemType = 0x08;
    constexpr size_t DefaultItem_ItemDefinition = 0x18;
    constexpr size_t DefaultItem_DefaultAttachments = 0x20;
    constexpr size_t DefaultItem_Count = 0x1E0;
    constexpr size_t AddDefaultItemParamsSize = 0x2E8;
    constexpr size_t AddDefaultItem_MainContainerIndex = 0x1E8;
    constexpr size_t AddDefaultItem_InContainerIndex = 0x1EC;
    constexpr size_t AddDefaultItem_ReturnValue = 0x1F0;

    uintptr_t g_cachedWorld = 0;
    uintptr_t g_cachedPlayerInventory = 0;
    uintptr_t g_cachedStashInventory = 0;

    InventoryService::Result g_lastResult{};

    struct WeaponPresetMapping
    {
        int32_t DefinitionIndex = 0;
        int32_t PresetIndex = 0;
    };

    // Current Dumper-7 IRRItemDefinition -> IRRItemPreset pairs. A complete gun
    // must be built from the preset: the base definition alone is only the root
    // receiver and produces an immovable/incomplete inventory record.
    constexpr std::array<WeaponPresetMapping, 11> WeaponPresets = {{
        { 0x14493, 0x14472 }, // AK-104
        { 0x1482C, 0x1481F }, // Remington M700
        { 0x14898, 0x14886 }, // Glock 19
        { 0x14483, 0x14484 }, // AK-74M
        { 0x14710, 0x14711 }, // QSZ-92
        { 0x147D4, 0x147D5 }, // AKS-74U
        { 0x146B2, 0x146B3 }, // Simonov SKS
        { 0x14603, 0x14604 }, // MP5SD
        { 0x1456F, 0x14570 }, // Colt M4A1
        { 0x143CE, 0x143CF }, // KRISS Vector .45
        { 0x14301, 0x142F9 }  // RIA VR80
    }};

    int32_t FindWeaponPresetIndex(int32_t definitionIndex)
    {
        const auto found = std::find_if(WeaponPresets.begin(), WeaponPresets.end(),
            [definitionIndex](const WeaponPresetMapping& mapping)
            {
                return mapping.DefinitionIndex == definitionIndex;
            });
        return found == WeaponPresets.end() ? 0 : found->PresetIndex;
    }

    bool PlausibleArray(const TArrayHeader& array, int32_t maxCount)
    {
        if (array.Count < 0 || array.Capacity < array.Count || array.Capacity > maxCount)
            return false;
        if (array.Count == 0)
            return true;
        return array.Data && Memory::IsReadable(array.Data, 1);
    }

    uintptr_t ResolveInventoryThroughLibrary(int32_t functionIndex)
    {
        const uintptr_t library = GameAccess::GetObjectByIndex(ObjectIndices::DefaultInventoryFunctionLibrary);
        if (!library)
            return 0;

        const std::array<uintptr_t, 4> contexts = {
            GameAccess::GetLocalPawn(), GameAccess::GetLocalController(),
            GameAccess::GetGameInstance(), GameAccess::GetWorld()
        };

        for (const uintptr_t context : contexts)
        {
            if (!context)
                continue;
            struct Params
            {
                uintptr_t WorldContextObject = 0;
                uintptr_t ReturnValue = 0;
            } params{};
            params.WorldContextObject = context;
            if (GameAccess::InvokeFunctionRaw(library, functionIndex, &params, sizeof(params)) &&
                params.ReturnValue && Memory::IsReadable(params.ReturnValue, sizeof(uintptr_t)))
                return params.ReturnValue;
        }
        return 0;
    }

    int32_t GetMainContainerCount(uintptr_t component)
    {
        if (!component)
            return 0;
        TArrayHeader main{};
        if (!Memory::TryRead(component + Offsets::InventoryComponent_MainContainers, main) ||
            !PlausibleArray(main, 128))
            return 0;
        return main.Count;
    }

    bool BuildDefaultItem(const ItemCatalog::Entry& entry, uintptr_t definition,
                          int amount,
                          std::array<unsigned char, DefaultItemStride>& defaultItem,
                          InventoryService::Result& result)
    {
        defaultItem.fill(0);
        const int32_t presetIndex = FindWeaponPresetIndex(entry.DefinitionIndex);
        if (presetIndex)
        {
            const uintptr_t preset = GameAccess::GetObjectByIndex(presetIndex);
            if (!preset || !Memory::IsReadable(preset, sizeof(uintptr_t)))
                return false;

            if (!GameAccess::InvokeFunctionRaw(preset,
                    FunctionIndices::IRRItemPreset_GetDefaultItem,
                    defaultItem.data(), defaultItem.size()))
                return false;

            uintptr_t presetDefinition = 0;
            std::memcpy(&presetDefinition,
                defaultItem.data() + DefaultItem_ItemDefinition,
                sizeof(presetDefinition));
            if (presetDefinition != definition)
                return false;

            result.CompleteWeapon = true;
            result.PresetObject = preset;
        }
        else
        {
            // EDefaultItemType::ItemDefinition is value 1 in this build.
            const uint8_t definitionType = 1;
            std::memcpy(defaultItem.data() + DefaultItem_ItemType,
                &definitionType, sizeof(definitionType));
            std::memcpy(defaultItem.data() + DefaultItem_ItemDefinition,
                &definition, sizeof(definition));
        }

        const int32_t clamped = std::clamp(amount, 1, 1000000);
        std::memcpy(defaultItem.data() + DefaultItem_Count,
            &clamped, sizeof(clamped));

        uint8_t itemType = 0;
        std::memcpy(&itemType, defaultItem.data() + DefaultItem_ItemType,
            sizeof(itemType));
        result.DefaultItemType = static_cast<int32_t>(itemType);

        TArrayHeader attachments{};
        std::memcpy(&attachments,
            defaultItem.data() + DefaultItem_DefaultAttachments,
            sizeof(attachments));
        if (!PlausibleArray(attachments, 128))
            return false;
        result.ExpectedAttachments = attachments.Count;

        // All mapped firearm presets in the current dump contain their vital
        // parts. Refuse to create a bare receiver if a stale/mismatched preset
        // unexpectedly resolves without attachments.
        return !result.CompleteWeapon || result.ExpectedAttachments > 0;
    }

    bool AddDefaultItem(uintptr_t component,
                        const std::array<unsigned char, DefaultItemStride>& defaultItem,
                        int32_t mainContainerIndex, int32_t inContainerIndex,
                        std::array<unsigned char, ContainerItemStride>& returnedItem,
                        uintptr_t& returnedDefinition)
    {
        returnedItem.fill(0);
        returnedDefinition = 0;
        if (!component)
            return false;

        alignas(8) std::array<unsigned char, AddDefaultItemParamsSize> params{};
        std::memcpy(params.data(), defaultItem.data(), defaultItem.size());
        std::memcpy(params.data() + AddDefaultItem_MainContainerIndex, &mainContainerIndex, sizeof(mainContainerIndex));
        std::memcpy(params.data() + AddDefaultItem_InContainerIndex, &inContainerIndex, sizeof(inContainerIndex));

        if (!GameAccess::InvokeFunctionRaw(component, FunctionIndices::InventoryComponent_AddDefaultItem,
                                           params.data(), params.size()))
            return false;

        std::memcpy(returnedItem.data(),
                    params.data() + AddDefaultItem_ReturnValue,
                    returnedItem.size());
        std::memcpy(&returnedDefinition,
                    returnedItem.data() + ContainerItem_Definition,
                    sizeof(returnedDefinition));
        return true;
    }

    bool CanAddBuiltItem(uintptr_t component,
                         const std::array<unsigned char, ContainerItemStride>& item,
                         bool& outCanAdd)
    {
        outCanAdd = false;
        if (!component)
            return false;
        alignas(8) std::array<unsigned char, 0x100> params{};
        std::memcpy(params.data(), item.data(), item.size());
        if (!GameAccess::InvokeFunctionRaw(component,
                FunctionIndices::InventoryComponent_CanAddToInventory,
                params.data(), params.size()))
            return false;
        outCanAdd = params[0xF8] != 0;
        return true;
    }

    bool TryAddBuiltItem(uintptr_t component,
                         std::array<unsigned char, ContainerItemStride> item,
                         bool& outAdded)
    {
        outAdded = false;
        if (!component)
            return false;

        uintptr_t itemInventory = 0;
        std::memcpy(&itemInventory, item.data() + 0x08, sizeof(itemInventory));
        if (!itemInventory)
            std::memcpy(item.data() + 0x08, &component, sizeof(component));

        alignas(8) std::array<unsigned char, 0x108> params{};
        std::memcpy(params.data(), &component, sizeof(component));
        std::memcpy(params.data() + 0x08, item.data(), item.size());
        if (!GameAccess::InvokeFunctionRaw(component,
                FunctionIndices::InventoryComponent_TryAddItem,
                params.data(), params.size()))
            return false;
        outAdded = params[0x101] != 0;
        return true;
    }

    bool AddResourceFallback(const ItemCatalog::Entry& entry, int amount)
    {
        const uintptr_t gameInstance = GameAccess::GetGameInstance();
        if (!gameInstance)
            return false;
        FName name{};
        if (!GameAccess::GetObjectNameToken(entry.DefinitionIndex, name))
            return false;
        struct alignas(8) Params
        {
            FName Name{};
            int32_t Amount = 0;
            int32_t Padding = 0;
        } params{};
        params.Name = name;
        params.Amount = std::clamp(amount, 1, 1000000);
        return GameAccess::InvokeFunctionRaw(gameInstance,
            FunctionIndices::GeneralGameInstance_AddResource,
            &params, sizeof(params));
    }
}

namespace InventoryService
{
    void ResetCache()
    {
        g_cachedWorld = 0;
        g_cachedPlayerInventory = 0;
        g_cachedStashInventory = 0;
    }

    uintptr_t GetPlayerInventory()
    {
        const uintptr_t world = GameAccess::GetWorld();
        if (world != g_cachedWorld)
            ResetCache();
        g_cachedWorld = world;

        if (g_cachedPlayerInventory && Memory::IsReadable(g_cachedPlayerInventory, sizeof(uintptr_t)))
            return g_cachedPlayerInventory;

        const auto& d = GameAccess::GetDiagnostics();
        if (d.InventoryComponent && Memory::IsReadable(d.InventoryComponent, sizeof(uintptr_t)))
            g_cachedPlayerInventory = d.InventoryComponent;
        if (!g_cachedPlayerInventory)
            g_cachedPlayerInventory = ResolveInventoryThroughLibrary(
                FunctionIndices::InventoryFunctionLibrary_GetPlayerInventoryComponent);
        return g_cachedPlayerInventory;
    }

    uintptr_t GetStashInventory()
    {
        const uintptr_t world = GameAccess::GetWorld();
        if (world != g_cachedWorld)
            ResetCache();
        g_cachedWorld = world;

        if (g_cachedStashInventory && Memory::IsReadable(g_cachedStashInventory, sizeof(uintptr_t)))
            return g_cachedStashInventory;
        g_cachedStashInventory = ResolveInventoryThroughLibrary(
            FunctionIndices::InventoryFunctionLibrary_GetStashInventoryComponent);
        return g_cachedStashInventory;
    }

    InventoryProbe ProbeInventory(uintptr_t inventoryComponent)
    {
        InventoryProbe probe{};
        probe.Component = inventoryComponent;
        if (!inventoryComponent)
            return probe;
        TArrayHeader main{};
        if (!Memory::TryRead(inventoryComponent + Offsets::InventoryComponent_MainContainers, main) ||
            !PlausibleArray(main, 128))
            return probe;
        probe.MainArrayData = main.Data;
        probe.MainContainerCount = main.Count;
        probe.MainContainerCapacity = main.Capacity;
        probe.MainArrayValid = true;
        for (int32_t i = 0; i < main.Count; ++i)
        {
            TArrayHeader items{};
            const uintptr_t mainAddress = main.Data +
                static_cast<uintptr_t>(i) * MainContainerStride;
            if (Memory::TryRead(mainAddress + MainContainer_Items, items) &&
                PlausibleArray(items, 4096))
                probe.TotalContainerItems += items.Count;
        }
        return probe;
    }

    const Result& GetLastResult()
    {
        return g_lastResult;
    }

    bool IsCompleteWeapon(const ItemCatalog::Entry& entry)
    {
        return FindWeaponPresetIndex(entry.DefinitionIndex) != 0;
    }

    int64_t CountItem(uintptr_t inventoryComponent, uintptr_t itemDefinition)
    {
        if (!inventoryComponent || !itemDefinition)
            return 0;

        TArrayHeader main{};
        if (!Memory::TryRead(inventoryComponent + Offsets::InventoryComponent_MainContainers, main) ||
            !PlausibleArray(main, 128))
            return 0;

        int64_t total = 0;
        for (int32_t i = 0; i < main.Count; ++i)
        {
            const uintptr_t mainAddress = main.Data + static_cast<uintptr_t>(i) * MainContainerStride;
            TArrayHeader items{};
            if (!Memory::TryRead(mainAddress + MainContainer_Items, items) || !PlausibleArray(items, 4096))
                continue;
            for (int32_t j = 0; j < items.Count; ++j)
            {
                const uintptr_t itemAddress = items.Data + static_cast<uintptr_t>(j) * ContainerItemStride;
                uintptr_t definition = 0;
                if (!Memory::TryRead(itemAddress + ContainerItem_Definition, definition) ||
                    definition != itemDefinition)
                    continue;
                int32_t count = 1;
                Memory::TryRead(itemAddress + ContainerItem_Count, count);
                total += std::max(count, 1);
            }
        }
        return total;
    }

    void RefreshInventory(uintptr_t inventoryComponent)
    {
        if (!inventoryComponent)
            return;
        GameAccess::InvokeFunctionRaw(inventoryComponent,
            FunctionIndices::InventoryComponent_ItemsUpdated, nullptr, 0);
        GameAccess::InvokeFunctionRaw(inventoryComponent,
            FunctionIndices::InventoryComponent_UpdateCurrency, nullptr, 0);
        GameAccess::InvokeFunctionRaw(inventoryComponent,
            FunctionIndices::InventoryComponent_OnRepMainContainers, nullptr, 0);
    }

    bool AddItem(const ItemCatalog::Entry& entry, int amount, Destination destination, Result* outResult)
    {
        Result result{};
        const int clamped = std::clamp(amount, 1, 1000000);
        const uintptr_t definition = GameAccess::GetObjectByIndex(entry.DefinitionIndex);
        if (!definition)
        {
            std::snprintf(result.Message, sizeof(result.Message),
                "%s: live IRRItemDefinition 0x%X unavailable", entry.Id, entry.DefinitionIndex);
            g_lastResult = result;
            if (outResult) *outResult = result;
            return false;
        }

        if (destination == Destination::HideoutResource)
        {
            result.Backend = "GeneralGameInstance.AddResource";
            const bool dispatched = AddResourceFallback(entry, clamped);
            result.DispatchCount = dispatched ? 1 : 0;
            result.Success = false;
            std::snprintf(result.Message, sizeof(result.Message),
                "%s x%d -> resource | %s (UNVERIFIED; not reported as success)",
                entry.Id, clamped, dispatched ? "ProcessEvent dispatched" : "dispatch failed");
            g_lastResult = result;
            if (outResult) *outResult = result;
            return false;
        }

        const bool stash = destination == Destination::Stash;
        const uintptr_t component = stash ? GetStashInventory() : GetPlayerInventory();
        result.TargetComponent = component;
        if (!component)
        {
            std::snprintf(result.Message, sizeof(result.Message), "%s: %s component unavailable",
                entry.Id, stash ? "stash" : "player inventory");
            g_lastResult = result;
            if (outResult) *outResult = result;
            return false;
        }

        const InventoryProbe initialProbe = ProbeInventory(component);
        result.MainContainerCount = initialProbe.MainContainerCount;
        if (!initialProbe.MainArrayValid || initialProbe.MainContainerCount <= 0)
        {
            std::snprintf(result.Message, sizeof(result.Message),
                "%s: %s MainContainers invalid/empty (data 0x%llX, %d/%d)",
                entry.Id, stash ? "stash" : "inventory",
                static_cast<unsigned long long>(initialProbe.MainArrayData),
                initialProbe.MainContainerCount, initialProbe.MainContainerCapacity);
            g_lastResult = result;
            if (outResult) *outResult = result;
            return false;
        }

        const bool completeWeapon = IsCompleteWeapon(entry);
        const int requestedAmount = completeWeapon ? std::min(clamped, 10) : clamped;
        std::array<unsigned char, DefaultItemStride> defaultItem{};
        if (!BuildDefaultItem(entry, definition,
                completeWeapon ? 1 : requestedAmount, defaultItem, result))
        {
            std::snprintf(result.Message, sizeof(result.Message),
                "%s: %s construction failed; no bare/incomplete weapon was inserted",
                entry.Id, completeWeapon ? "complete weapon preset" : "default item");
            g_lastResult = result;
            if (outResult) *outResult = result;
            return false;
        }

        result.CountBefore = CountItem(component, definition);
        result.CountAfter = result.CountBefore;
        result.ItemRecordsBefore = initialProbe.TotalContainerItems;
        result.ItemRecordsAfter = initialProbe.TotalContainerItems;
        bool anyDispatch = false;
        bool incompleteWeaponDetected = false;
        int addedUnits = 0;
        const char* directBackend = completeWeapon ?
            "InventoryComponent.AddDefaultItem(preset)" :
            "InventoryComponent.AddDefaultItem(definition)";
        const char* lastDispatch = directBackend;

        const int32_t mainCount = GetMainContainerCount(component);
        std::vector<int32_t> candidates;
        candidates.reserve(static_cast<size_t>(mainCount) + 1);
        candidates.push_back(-1);
        for (int32_t i = 0; i < mainCount; ++i)
            candidates.push_back(i);

        const int unitsToCreate = completeWeapon ? requestedAmount : 1;
        for (int unit = 0; unit < unitsToCreate; ++unit)
        {
            const int64_t unitCountBefore = CountItem(component, definition);
            const int32_t unitRecordsBefore = ProbeInventory(component).TotalContainerItems;
            bool unitAdded = false;

            for (const int32_t containerIndex : candidates)
            {
                std::array<unsigned char, ContainerItemStride> returnedItem{};
                uintptr_t returnedDefinition = 0;
                if (!AddDefaultItem(component, defaultItem, containerIndex, -1,
                                    returnedItem, returnedDefinition))
                    continue;

                lastDispatch = directBackend;
                ++result.DispatchCount;
                anyDispatch = true;
                result.ReturnedDefinition = returnedDefinition;
                RefreshInventory(component);

                int64_t unitCountAfter = CountItem(component, definition);
                int32_t unitRecordsAfter = ProbeInventory(component).TotalContainerItems;
                const bool rootAdded = unitCountAfter > unitCountBefore;
                const int32_t addedRecords = unitRecordsAfter - unitRecordsBefore;
                const bool completePresetAdded = !completeWeapon ||
                    addedRecords >= result.ExpectedAttachments + 1;

                if (rootAdded && completePresetAdded)
                {
                    result.ContainerIndex = containerIndex;
                    result.Backend = directBackend;
                    unitAdded = true;
                    break;
                }

                if (completeWeapon && rootAdded)
                {
                    // Never feed just the returned root through TryAddItem: that
                    // strips its preset children and recreates the broken gun.
                    incompleteWeaponDetected = true;
                    break;
                }

                if (!completeWeapon && returnedDefinition == definition)
                {
                    bool canAdd = false;
                    const bool canAddQueried =
                        CanAddBuiltItem(component, returnedItem, canAdd);
                    if (canAddQueried)
                        result.CanAddBuiltItem = result.CanAddBuiltItem || canAdd;

                    if (canAddQueried && canAdd)
                    {
                        bool added = false;
                        if (TryAddBuiltItem(component, returnedItem, added))
                        {
                            ++result.DispatchCount;
                            result.TryAddReturned = result.TryAddReturned || added;
                            anyDispatch = true;
                            RefreshInventory(component);
                            unitCountAfter = CountItem(component, definition);
                            if (unitCountAfter > unitCountBefore)
                            {
                                lastDispatch =
                                    "InventoryComponent.TryAddItem(game-built definition)";
                                result.ContainerIndex = containerIndex;
                                result.Backend = lastDispatch;
                                unitAdded = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (!unitAdded)
                break;
            ++addedUnits;
        }

        result.CountAfter = CountItem(component, definition);
        result.ItemRecordsAfter = ProbeInventory(component).TotalContainerItems;
        result.Success = addedUnits == unitsToCreate && addedUnits > 0;
        if (!result.Success)
            result.Backend = lastDispatch;

        if (incompleteWeaponDetected)
        {
            std::snprintf(result.Message, sizeof(result.Message),
                "%s -> %s | BLOCKED: root appeared without %d preset attachments",
                entry.Id, stash ? "stash" : "inventory",
                result.ExpectedAttachments);
        }
        else
        {
            std::snprintf(result.Message, sizeof(result.Message),
                "%s x%d -> %s | %s | roots %lld -> %lld | records %d -> %d | attachments %d",
                entry.Id, requestedAmount, stash ? "stash" : "inventory",
                result.Success ? result.Backend :
                    (anyDispatch ? "NO VERIFIED CONTAINER INSERTION" : "NO BACKEND DISPATCH"),
                static_cast<long long>(result.CountBefore),
                static_cast<long long>(result.CountAfter),
                result.ItemRecordsBefore, result.ItemRecordsAfter,
                result.ExpectedAttachments);
        }
        g_lastResult = result;
        if (outResult) *outResult = result;
        return result.Success;
    }

}
