#pragma once

#include <cstdint>

namespace ItemCatalog { struct Entry; }

namespace InventoryService
{
    enum class Destination
    {
        PlayerInventory = 0,
        Stash = 1,
        HideoutResource = 2
    };

    struct Result
    {
        bool Success = false;
        uintptr_t TargetComponent = 0;
        int32_t ContainerIndex = -1;
        int32_t MainContainerCount = 0;
        int32_t DispatchCount = 0;
        int32_t DefaultItemType = -1;
        int64_t CountBefore = 0;
        int64_t CountAfter = 0;
        int32_t ItemRecordsBefore = 0;
        int32_t ItemRecordsAfter = 0;
        int32_t ExpectedAttachments = 0;
        uintptr_t PresetObject = 0;
        uintptr_t ReturnedDefinition = 0;
        bool CompleteWeapon = false;
        bool CanAddBuiltItem = false;
        bool TryAddReturned = false;
        const char* Backend = "none";
        char Message[256]{};
    };

    struct InventoryProbe
    {
        uintptr_t Component = 0;
        uintptr_t MainArrayData = 0;
        int32_t MainContainerCount = 0;
        int32_t MainContainerCapacity = 0;
        int32_t TotalContainerItems = 0;
        bool MainArrayValid = false;
    };

    uintptr_t GetPlayerInventory();
    uintptr_t GetStashInventory();
    int64_t CountItem(uintptr_t inventoryComponent, uintptr_t itemDefinition);
    InventoryProbe ProbeInventory(uintptr_t inventoryComponent);
    const Result& GetLastResult();
    bool IsCompleteWeapon(const ItemCatalog::Entry& entry);
    bool AddItem(const ItemCatalog::Entry& entry, int amount, Destination destination, Result* outResult = nullptr);
    void RefreshInventory(uintptr_t inventoryComponent);
    void ResetCache();
}
