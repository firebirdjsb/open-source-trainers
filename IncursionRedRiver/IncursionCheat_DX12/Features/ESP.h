#pragma once

namespace ESP
{
    extern bool bEnabled;
    extern bool bDrawBoxes;
    extern bool bShowHealth;
    extern bool bShowNames;
    extern bool bShowDistance;
    extern float maxDistanceMeters;
    extern int maxActors;

    void Init();
    void Enable();
    void Disable();
    void Toggle();
    bool IsEnabled();
    void Run();
    void Render();
}
