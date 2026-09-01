#pragma once

namespace UECheats
{
    extern bool bGodMode;
    extern bool bInfiniteAmmo;
    extern bool bNoReload;
    extern bool bSpeedHack;
    extern bool bInfiniteStamina;
    extern bool bNoRecoil;
    extern bool bNoWeaponSway;
    extern bool bNoMalfunctions;
    extern bool bHighCarryWeight;
    extern bool bInvisible;
    extern bool bBulletDebugTraces;
    extern bool bFly;
    extern bool bNoClip;
    extern float speedMultiplier;
    extern float carryWeight;
    extern float flySpeed;

    void EnableGodMode();
    void DisableGodMode();
    void EnableInfiniteAmmo();
    void DisableInfiniteAmmo();
    void EnableNoReload();
    void DisableNoReload();
    void EnableSpeedHack();
    void DisableSpeedHack();

    void ProcessTick();
    void Shutdown();
    void RenderTab();
}
