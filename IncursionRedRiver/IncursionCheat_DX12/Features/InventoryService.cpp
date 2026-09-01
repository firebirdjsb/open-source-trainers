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
    constexpr size_t AddDefaultItemParamsSize = 0x2E8;
    constexpr size_t AddDefaultItem_MainContainerIndex = 0x1E8;
    constexpr size_t AddDefaultItem_InContainerIndex = 0x1EC;
    constexpr size_t AddDefaultItem_ReturnValue = 0x1F0;

    uintptr_t g_cachedWorld = 0;
    uintptr_t g_cachedPlayerInventory = 0;
    uintptr_t g_cachedStashInventory = 0;

    InventoryService::Result g_lastResult{};

    bool PlausibleArray(const TArrayHeader& array, int32_t maxCount)
    {
        if (array.Count < 0 || array.Capacity < array.Count || array.Capacity > maxCount)
            return false;
        if (array.Count == 0)
            return true;
        return array.Data && Memory::IsReadable(array.Data, 1);
    }

    uintptr_t WorldContext()
    {
        if (const uintptr_t pawn = GameAccess::GetLocalPawn())
            return pawn;
        if (const uintptr_t controller = GameAccess::GetLocalController())
            return controller;
        if (const uintptr_t instance = GameAccess::GetGameInstance())
            return instance;
        return GameAccess::GetWorld();
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

    bool AddDefaultItem(uintptr_t component, uintptr_t definition, int amount,
                        int32_t mainContainerIndex, int32_t inContainerIndex,
                        uint8_t itemType,
                        std::array<unsigned char, ContainerItemStride>& returnedItem,
                        uintptr_t& returnedDefinition)
    {
        returnedItem.fill(0);
        returnedDefinition = 0;
        if (!component || !definition || amount <= 0)
            return false;

        alignas(8) std::array<unsigned char, AddDefaultItemParamsSize> params{};
        // EDefaultItemType names are not emitted by Dumper-7. The old build sent
        // zero only and runtime testing showed a no-op dispatch, so the caller probes
        // the definition-oriented enum value first and falls back to zero.
        std::memcpy(params.data() + 0x08, &itemType, sizeof(itemType));
        std::memcpy(params.data() + 0x18, &definition, sizeof(definition));
        const int32_t clamped = std::clamp(amount, 1, 1000000);
        std::memcpy(params.data() + 0x1E0, &clamped, sizeof(clamped));
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

    bool AddByRowNameFallback(const ItemCatalog::Entry& entry, int amount)
    {
        const uintptr_t library = GameAccess::GetObjectByIndex(ObjectIndices::DefaultInventoryFunctionLibrary);
        const uintptr_t context = WorldContext();
        if (!library || !context)
            return false;

        FName name{};
        if (!GameAccess::GetObjectNameToken(entry.DefinitionIndex, name))
            return false;

        struct alignas(8) Params
        {
            uintptr_t WorldContextObject = 0;
            FName Name{};
            int32_t Count = 0;
            int32_t Padding = 0;
        } params{};
        params.WorldContextObject = context;
        params.Name = name;
        params.Count = std::clamp(amount, 1, 1000000);
        return GameAccess::InvokeFunctionRaw(library,
            FunctionIndices::InventoryFunctionLibrary_AddItemByRowName,
            &params, sizeof(params));
    }


    bool AddThroughGameInstance(const ItemCatalog::Entry& entry, int amount)
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
            FunctionIndices::GeneralGameInstance_AddItem, &params, sizeof(params));
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

        result.CountBefore = CountItem(component, definition);
        result.CountAfter = result.CountBefore;
        bool anyDispatch = false;
        const char* lastDispatch = "none";

        auto verifyChange = [&](const char* backend, int32_t containerIndex) -> bool
        {
            RefreshInventory(component);
            const int64_t after = CountItem(component, definition);
            result.CountAfter = after;
            if (after > result.CountBefore)
            {
                result.Success = true;
                result.Backend = backend;
                result.ContainerIndex = containerIndex;
                return true;
            }
            return false;
        };

        if (!stash)
        {
            if (AddByRowNameFallback(entry, clamped))
            {
                ++result.DispatchCount;
                anyDispatch = true;
                lastDispatch = "InventoryFunctionLibrary.AddItemByRowName";
                if (verifyChange(lastDispatch, -1))
                    goto done;
            }
        }

        if (AddThroughGameInstance(entry, clamped))
        {
            ++result.DispatchCount;
            anyDispatch = true;
            lastDispatch = "GeneralGameInstance.AddItem";
            if (verifyChange(lastDispatch, -1))
                goto done;
        }

        {
            const int32_t mainCount = GetMainContainerCount(component);
            std::vector<int32_t> candidates;
            candidates.reserve(static_cast<size_t>(mainCount) + 1);
            candidates.push_back(-1);
            for (int32_t i = 0; i < mainCount; ++i)
                candidates.push_back(i);

            for (const uint8_t itemType : { uint8_t{1}, uint8_t{0} })
            {
                for (const int32_t containerIndex : candidates)
                {
                    std::array<unsigned char, ContainerItemStride> returnedItem{};
                    uintptr_t returnedDefinition = 0;
                    if (!AddDefaultItem(component, definition, clamped, containerIndex, -1,
                                        itemType, returnedItem, returnedDefinition))
                        continue;

                    ++result.DispatchCount;
                    anyDispatch = true;
                    result.DefaultItemType = static_cast<int32_t>(itemType);
                    result.ReturnedDefinition = returnedDefinition;
                    lastDispatch = "InventoryComponent.AddDefaultItem";
                    if (verifyChange(lastDispatch, containerIndex))
                        goto done;

                    if (returnedDefinition == definition)
                    {
                        bool canAdd = false;
                        const bool canAddQueried = CanAddBuiltItem(component, returnedItem, canAdd);
                        if (canAddQueried)
                            result.CanAddBuiltItem = result.CanAddBuiltItem || canAdd;

                        // Only enter TryAddItem when the game's own CanAddToInventory
                        // accepted the game-built ContainerItem. This keeps the fallback
                        // conservative and avoids feeding an invalid item into native code.
                        if (canAddQueried && canAdd)
                        {
                            bool added = false;
                            if (TryAddBuiltItem(component, returnedItem, added))
                            {
                                ++result.DispatchCount;
                                result.TryAddReturned = result.TryAddReturned || added;
                                anyDispatch = true;
                                lastDispatch = "InventoryComponent.TryAddItem(game-built return)";
                                if (verifyChange(lastDispatch, containerIndex))
                                    goto done;
                            }
                        }
                    }
                }
            }
        }

    done:
        if (!result.Success)
        {
            result.Backend = lastDispatch;
            result.CountAfter = CountItem(component, definition);
        }

        std::snprintf(result.Message, sizeof(result.Message),
            "%s x%d -> %s | %s | count %lld -> %lld | containers %d | type %d | dispatches %d",
            entry.Id, clamped, stash ? "stash" : "inventory",
            result.Success ? result.Backend :
                (anyDispatch ? "DISPATCHED BUT NO VERIFIED INVENTORY CHANGE" : "NO BACKEND DISPATCH"),
            static_cast<long long>(result.CountBefore), static_cast<long long>(result.CountAfter),
            result.MainContainerCount, result.DefaultItemType, result.DispatchCount);
        g_lastResult = result;
        if (outResult) *outResult = result;
        return result.Success;
    }

}
