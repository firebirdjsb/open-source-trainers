#pragma once
#include <cstdint>

class CheatManager
{
public:
    static CheatManager& Get();

    bool Initialize();
    void Shutdown();
    void Tick();

    uintptr_t GetWorld() const;
    uintptr_t GetLocalPlayerController() const;
    uintptr_t GetLocalPawn() const;
    bool IsInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
    uint64_t m_lastRuntimeRefreshMs = 0;
};

#define g_CheatManager CheatManager::Get()
