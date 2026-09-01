#include <Windows.h>

#include <iostream>

int wmain()
{
    wchar_t eventName[128]{};
    swprintf_s(eventName, L"Local\\IncursionLoaderTest_%lu", GetCurrentProcessId());
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, eventName);
    if (!event)
    {
        std::wcerr << L"CreateEventW failed: " << GetLastError() << L"\n";
        return 1;
    }

    std::wcout << L"Loader test host PID " << GetCurrentProcessId()
               << L" waiting for test DLL.\n";
    const DWORD wait = WaitForSingleObject(event, 30000);
    CloseHandle(event);
    if (wait != WAIT_OBJECT_0)
    {
        std::wcerr << L"Test DLL did not signal the host.\n";
        return 2;
    }

    std::wcout << L"Test DLL loaded and signaled successfully.\n";
    // Stay alive briefly so the loader's duplicate-injection path can also be tested.
    Sleep(15000);
    return 0;
}
