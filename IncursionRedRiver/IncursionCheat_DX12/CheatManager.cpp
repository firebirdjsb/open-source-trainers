#include "CheatManager.h"

#include "Memory/Memory.h"
#include "sdk/GameAccess.h"
#include "Features/ItemMagnet.h"
#include "Features/Aimbot.h"
#include "Features/ESP.h"
#include "Features/UECheats.h"
#include "Features/TacticalTools.h"
#include "Features/MovementTools.h"
#include "Features/WorldTools.h"
#include "Menu.h"

#include <Windows.h>

CheatManager& CheatManager::Get()
{
    static CheatManager instance;
    return instance;
}

bool CheatManager::Initialize()
{
    if (m_initialized)
        return true;
    m_initialized = Memory::Initialize();
    return m_initialized;
}

void CheatManager::Shutdown()
{
    if (!m_initialized)
        return;
    ItemMagnet::Shutdown();
    TacticalTools::Shutdown();
    MovementTools::Shutdown();
    WorldTools::Shutdown();
    UECheats::Shutdown();
    GameAccess::Reset();
    m_lastRuntimeRefreshMs = 0;
    m_initialized = false;
}

void CheatManager::Tick()
{
    if (!m_initialized)
        return;

    const uint64_t now = GetTickCount64();
    // Full world/local-player/actor acquisition scans thousands of live actors and
    // must not run at render frequency. Actor transforms, health and camera state
    // are still sampled by the lightweight feature paths every frame; this cache
    // only needs to track membership and pointer changes. During startup use a
    // faster retry so loader-before-game acquisition remains responsive.
    const uint64_t refreshInterval = Menu::bOpen ? 250u :
        (GameAccess::GetLocalPawn() ? 100u : 25u);
    if (!m_lastRuntimeRefreshMs || now - m_lastRuntimeRefreshMs >= refreshInterval)
    {
        GameAccess::Refresh();
        m_lastRuntimeRefreshMs = now;
    }
    Aimbot::Run();
    ESP::Run();
    UECheats::ProcessTick();
    ItemMagnet::ProcessTick();
    MovementTools::ProcessTick();
    WorldTools::ProcessTick();
    TacticalTools::ProcessFrame();
}

uintptr_t CheatManager::GetWorld() const { return GameAccess::GetWorld(); }
uintptr_t CheatManager::GetLocalPlayerController() const { return GameAccess::GetLocalController(); }
uintptr_t CheatManager::GetLocalPawn() const { return GameAccess::GetLocalPawn(); }
