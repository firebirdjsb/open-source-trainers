#include "hooks/PresentHook.h"
#include <Windows.h>

namespace
{
    DWORD WINAPI InitThread(LPVOID)
    {
        InitializeHook();
        return 0;
    }
}

extern "C" __declspec(dllexport) void InitializeCheat()
{
    HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
}

extern "C" __declspec(dllexport) void ShutdownCheat()
{
    // Call this export from a normal thread before FreeLibrary if manual unloading is required.
    ShutdownHook();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    // Deliberately no hook/ImGui/COM teardown under the Windows loader lock.
    return TRUE;
}
