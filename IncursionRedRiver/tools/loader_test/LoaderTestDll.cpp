#include <Windows.h>

#include <cwchar>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        wchar_t eventName[128]{};
        swprintf_s(eventName, L"Local\\IncursionLoaderTest_%lu", GetCurrentProcessId());
        HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
        if (event)
        {
            SetEvent(event);
            CloseHandle(event);
        }
    }
    return TRUE;
}
