#include <Windows.h>
#include <TlHelp32.h>

#include <cerrno>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    constexpr wchar_t DefaultTargetExe[] = L"Test_C-Win64-Shipping.exe";
    constexpr DWORD ProcessPollMilliseconds = 250;
    constexpr DWORD ReadyTimeoutMilliseconds = 60000;
    constexpr DWORD InjectionTimeoutMilliseconds = 30000;

    class UniqueHandle
    {
    public:
        UniqueHandle() = default;
        explicit UniqueHandle(HANDLE value) : value_(value) {}
        ~UniqueHandle() { Reset(); }
        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;
        UniqueHandle(UniqueHandle&& other) noexcept : value_(other.Release()) {}
        UniqueHandle& operator=(UniqueHandle&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                value_ = other.Release();
            }
            return *this;
        }
        HANDLE Get() const { return value_; }
        explicit operator bool() const
        {
            return value_ && value_ != INVALID_HANDLE_VALUE;
        }
        HANDLE Release()
        {
            const HANDLE value = value_;
            value_ = nullptr;
            return value;
        }
        void Reset(HANDLE value = nullptr)
        {
            if (*this)
                CloseHandle(value_);
            value_ = value;
        }

    private:
        HANDLE value_ = nullptr;
    };

    struct RemoteModule
    {
        uintptr_t Base = 0;
        fs::path Path;
    };

    struct Options
    {
        fs::path DllPath;
        fs::path GamePath;
        std::wstring TargetExe = DefaultTargetExe;
        bool TargetExeExplicit = false;
        bool WaitForGame = true;
        unsigned long long TimeoutSeconds = 0; // Zero means no timeout.
        unsigned long long SettleMilliseconds = 2000;
    };

    enum class InjectionResult
    {
        Failed,
        Loaded,
        AlreadyLoaded
    };

    std::wstring Win32Message(DWORD error)
    {
        wchar_t* buffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::wstring message = length && buffer ? std::wstring(buffer, length) : L"unknown error";
        if (buffer)
            LocalFree(buffer);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' ||
                                    message.back() == L' ' || message.back() == L'.'))
            message.pop_back();
        return message;
    }

    void PrintWin32Error(const wchar_t* operation, DWORD error = GetLastError())
    {
        std::wcerr << L"[Error] " << operation << L" failed (" << error << L"): "
                   << Win32Message(error) << L"\n";
    }

    fs::path AbsoluteNormalized(const fs::path& path)
    {
        std::error_code error;
        fs::path result = fs::absolute(path, error);
        if (error)
            result = path;
        const fs::path canonical = fs::weakly_canonical(result, error);
        return error ? result.lexically_normal() : canonical;
    }

    fs::path GetSelfDirectory()
    {
        std::vector<wchar_t> buffer(32768);
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length || length >= buffer.size())
            return fs::current_path();
        return fs::path(std::wstring(buffer.data(), length)).parent_path();
    }

    bool EnableDebugPrivilege()
    {
        UniqueHandle token;
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                              &rawToken))
            return false;
        token.Reset(rawToken);

        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME,
                                   &privileges.Privileges[0].Luid))
            return false;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(ERROR_SUCCESS);
        if (!AdjustTokenPrivileges(token.Get(), FALSE, &privileges, sizeof(privileges),
                                   nullptr, nullptr))
            return false;
        return GetLastError() == ERROR_SUCCESS;
    }

    DWORD FindProcessId(const std::wstring& name)
    {
        UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snapshot)
            return 0;

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (!Process32FirstW(snapshot.Get(), &entry))
            return 0;
        do
        {
            if (_wcsicmp(entry.szExeFile, name.c_str()) == 0)
                return entry.th32ProcessID;
        } while (Process32NextW(snapshot.Get(), &entry));
        return 0;
    }

    RemoteModule FindRemoteModule(DWORD processId, const std::wstring& moduleName)
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            UniqueHandle snapshot(CreateToolhelp32Snapshot(
                TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId));
            if (!snapshot)
            {
                if (GetLastError() == ERROR_BAD_LENGTH)
                {
                    Sleep(25);
                    continue;
                }
                return {};
            }

            MODULEENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Module32FirstW(snapshot.Get(), &entry))
            {
                do
                {
                    if (_wcsicmp(entry.szModule, moduleName.c_str()) == 0)
                        return { reinterpret_cast<uintptr_t>(entry.modBaseAddr),
                                 fs::path(entry.szExePath) };
                } while (Module32NextW(snapshot.Get(), &entry));
            }
            return {};
        }
        return {};
    }

    bool IsProcessAlive(DWORD processId)
    {
        UniqueHandle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, processId));
        return process && WaitForSingleObject(process.Get(), 0) == WAIT_TIMEOUT;
    }

    bool Is64BitProcess(HANDLE process)
    {
        using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
        const auto isWow64Process2 = reinterpret_cast<IsWow64Process2Fn>(GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
        if (isWow64Process2)
        {
            USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            if (!isWow64Process2(process, &processMachine, &nativeMachine))
                return false;
            const USHORT effectiveMachine = processMachine == IMAGE_FILE_MACHINE_UNKNOWN ?
                nativeMachine : processMachine;
            return effectiveMachine == IMAGE_FILE_MACHINE_AMD64;
        }

        BOOL wow64 = FALSE;
        return IsWow64Process(process, &wow64) && !wow64;
    }

    bool ValidateDllImage(const fs::path& dll)
    {
        std::ifstream input(dll, std::ios::binary);
        if (!input)
        {
            std::wcerr << L"[Error] Cannot open DLL: " << dll << L"\n";
            return false;
        }

        IMAGE_DOS_HEADER dos{};
        input.read(reinterpret_cast<char*>(&dos), sizeof(dos));
        if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
            dos.e_lfanew > 16 * 1024 * 1024)
        {
            std::wcerr << L"[Error] File is not a valid PE image: " << dll << L"\n";
            return false;
        }

        input.seekg(dos.e_lfanew, std::ios::beg);
        DWORD signature = 0;
        IMAGE_FILE_HEADER fileHeader{};
        WORD optionalMagic = 0;
        input.read(reinterpret_cast<char*>(&signature), sizeof(signature));
        input.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        input.read(reinterpret_cast<char*>(&optionalMagic), sizeof(optionalMagic));
        if (!input || signature != IMAGE_NT_SIGNATURE ||
            fileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            !(fileHeader.Characteristics & IMAGE_FILE_DLL) ||
            optionalMagic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            std::wcerr << L"[Error] DLL must be a valid x64 Windows DLL: " << dll << L"\n";
            return false;
        }
        return true;
    }

    LPTHREAD_START_ROUTINE ResolveRemoteLoadLibraryW(DWORD processId)
    {
        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const FARPROC localLoadLibrary = kernel32 ?
            GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
        if (!localLoadLibrary)
            return nullptr;

        // Kernel32 exports may be forwarded into KernelBase. Determine which local
        // module owns the function and apply that RVA to the same remote module.
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(localLoadLibrary), &memory,
                         sizeof(memory)) != sizeof(memory) || !memory.AllocationBase)
            return nullptr;

        const HMODULE localOwner = static_cast<HMODULE>(memory.AllocationBase);
        std::vector<wchar_t> ownerPath(32768);
        const DWORD ownerLength = GetModuleFileNameW(
            localOwner, ownerPath.data(), static_cast<DWORD>(ownerPath.size()));
        if (!ownerLength || ownerLength >= ownerPath.size())
            return nullptr;

        const std::wstring ownerName = fs::path(
            std::wstring(ownerPath.data(), ownerLength)).filename().wstring();
        const RemoteModule remoteOwner = FindRemoteModule(processId, ownerName);
        if (!remoteOwner.Base)
            return nullptr;

        const uintptr_t rva = reinterpret_cast<uintptr_t>(localLoadLibrary) -
                              reinterpret_cast<uintptr_t>(localOwner);
        return reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteOwner.Base + rva);
    }

    DWORD LaunchGame(const fs::path& executable)
    {
        std::error_code error;
        if (!fs::is_regular_file(executable, error))
        {
            std::wcerr << L"[Error] Game executable does not exist: " << executable << L"\n";
            return 0;
        }

        std::wstring command = L"\"" + executable.wstring() + L"\"";
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        const std::wstring workingDirectory = executable.parent_path().wstring();

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr,
                            FALSE, 0, nullptr, workingDirectory.c_str(), &startup, &process))
        {
            PrintWin32Error(L"CreateProcessW");
            return 0;
        }

        const DWORD processId = process.dwProcessId;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return processId;
    }

    bool TimeoutReached(ULONGLONG started, unsigned long long timeoutSeconds)
    {
        if (timeoutSeconds == 0)
            return false;
        const ULONGLONG elapsed = GetTickCount64() - started;
        const unsigned long long limit = timeoutSeconds >
            std::numeric_limits<ULONGLONG>::max() / 1000ULL ?
            std::numeric_limits<ULONGLONG>::max() : timeoutSeconds * 1000ULL;
        return elapsed >= limit;
    }

    DWORD WaitForProcess(const Options& options, ULONGLONG started)
    {
        DWORD processId = FindProcessId(options.TargetExe);
        if (processId || !options.WaitForGame)
            return processId;

        std::wcout << L"[Wait] Looking for " << options.TargetExe
                   << L". Start the game normally; Ctrl+C cancels the loader.\n";
        ULONGLONG nextStatus = GetTickCount64() + 5000;
        while (!TimeoutReached(started, options.TimeoutSeconds))
        {
            processId = FindProcessId(options.TargetExe);
            if (processId)
                return processId;
            if (GetTickCount64() >= nextStatus)
            {
                std::wcout << L"[Wait] Game has not appeared yet...\n";
                nextStatus = GetTickCount64() + 5000;
            }
            Sleep(ProcessPollMilliseconds);
        }
        return 0;
    }

    bool WaitForProcessReady(DWORD processId, const Options& options)
    {
        UniqueHandle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, processId));
        if (!process)
        {
            PrintWin32Error(L"OpenProcess readiness check");
            return false;
        }
        if (!Is64BitProcess(process.Get()))
        {
            std::wcerr << L"[Error] Target process is not x64; refusing to inject an x64 DLL.\n";
            return false;
        }

        std::wcout << L"[Wait] Process found (PID " << processId
                   << L"); waiting for its module loader to become ready...\n";
        const ULONGLONG started = GetTickCount64();
        bool modulesReady = false;
        while (GetTickCount64() - started < ReadyTimeoutMilliseconds)
        {
            if (WaitForSingleObject(process.Get(), 0) != WAIT_TIMEOUT)
            {
                std::wcerr << L"[Error] Target exited before injection.\n";
                return false;
            }
            const bool mainLoaded = FindRemoteModule(processId, options.TargetExe).Base != 0;
            const bool loaderReady = FindRemoteModule(processId, L"kernelbase.dll").Base != 0 ||
                                     FindRemoteModule(processId, L"kernel32.dll").Base != 0;
            if (mainLoaded && loaderReady)
            {
                modulesReady = true;
                break;
            }
            Sleep(100);
        }
        if (!modulesReady)
        {
            std::wcerr << L"[Error] Timed out waiting for the target module loader.\n";
            return false;
        }

        const ULONGLONG settleStarted = GetTickCount64();
        while (GetTickCount64() - settleStarted < options.SettleMilliseconds)
        {
            if (WaitForSingleObject(process.Get(), 100) != WAIT_TIMEOUT)
            {
                std::wcerr << L"[Error] Target exited during the startup delay.\n";
                return false;
            }
        }
        return true;
    }

    InjectionResult InjectLibrary(DWORD processId, const fs::path& dll)
    {
        const fs::path fullDll = AbsoluteNormalized(dll);
        const std::wstring moduleName = fullDll.filename().wstring();
        const RemoteModule existing = FindRemoteModule(processId, moduleName);
        if (existing.Base)
        {
            std::error_code equivalentError;
            if (!fs::equivalent(existing.Path, fullDll, equivalentError))
            {
                std::wcerr << L"[Error] A different DLL with the same module name is already loaded:\n"
                           << L"        loaded: " << existing.Path << L"\n"
                           << L"        requested: " << fullDll << L"\n";
                return InjectionResult::Failed;
            }
            std::wcout << L"[Info] DLL is already loaded at 0x" << std::hex
                       << existing.Base << std::dec << L": " << existing.Path << L"\n";
            return InjectionResult::AlreadyLoaded;
        }

        UniqueHandle process(OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
            FALSE, processId));
        if (!process)
        {
            PrintWin32Error(L"OpenProcess");
            std::wcerr << L"[Hint] If the game is elevated, run Loader.exe as administrator too.\n";
            return InjectionResult::Failed;
        }
        if (!Is64BitProcess(process.Get()))
        {
            std::wcerr << L"[Error] Loader, target, and DLL must all be x64.\n";
            return InjectionResult::Failed;
        }

        const std::wstring pathText = fullDll.wstring();
        const SIZE_T byteCount = (pathText.size() + 1) * sizeof(wchar_t);
        void* remotePath = VirtualAllocEx(process.Get(), nullptr, byteCount,
                                          MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remotePath)
        {
            PrintWin32Error(L"VirtualAllocEx");
            return InjectionResult::Failed;
        }

        bool remoteThreadCompleted = false;
        auto releaseRemotePath = [&]()
        {
            if (remoteThreadCompleted && remotePath)
            {
                VirtualFreeEx(process.Get(), remotePath, 0, MEM_RELEASE);
                remotePath = nullptr;
            }
        };

        SIZE_T written = 0;
        const BOOL writeSucceeded = WriteProcessMemory(
            process.Get(), remotePath, pathText.c_str(), byteCount, &written);
        if (!writeSucceeded || written != byteCount)
        {
            PrintWin32Error(L"WriteProcessMemory",
                            writeSucceeded ? ERROR_WRITE_FAULT : GetLastError());
            remoteThreadCompleted = true;
            releaseRemotePath();
            return InjectionResult::Failed;
        }

        const auto loadLibraryW = ResolveRemoteLoadLibraryW(processId);
        if (!loadLibraryW)
        {
            std::wcerr << L"[Error] Could not resolve remote LoadLibraryW.\n";
            remoteThreadCompleted = true;
            releaseRemotePath();
            return InjectionResult::Failed;
        }

        UniqueHandle thread(CreateRemoteThread(process.Get(), nullptr, 0, loadLibraryW,
                                                remotePath, 0, nullptr));
        if (!thread)
        {
            PrintWin32Error(L"CreateRemoteThread");
            remoteThreadCompleted = true;
            releaseRemotePath();
            return InjectionResult::Failed;
        }

        const DWORD waitResult = WaitForSingleObject(thread.Get(), InjectionTimeoutMilliseconds);
        if (waitResult != WAIT_OBJECT_0)
        {
            if (waitResult == WAIT_TIMEOUT)
            {
                std::wcerr << L"[Error] Remote LoadLibraryW did not finish within "
                           << InjectionTimeoutMilliseconds / 1000 << L" seconds.\n";
                std::wcerr << L"[Info] The remote path allocation was intentionally left in place because the thread may still be using it.\n";
            }
            else
            {
                PrintWin32Error(L"WaitForSingleObject(remote thread)");
            }
            return InjectionResult::Failed;
        }
        remoteThreadCompleted = true;

        DWORD threadExitCode = 0;
        if (!GetExitCodeThread(thread.Get(), &threadExitCode))
            PrintWin32Error(L"GetExitCodeThread");
        releaseRemotePath();

        // Do not rely only on the remote thread's 32-bit exit code for a 64-bit
        // HMODULE. Verify that Windows actually registered the DLL in the process.
        RemoteModule loaded{};
        for (int attempt = 0; attempt < 50 && IsProcessAlive(processId); ++attempt)
        {
            loaded = FindRemoteModule(processId, moduleName);
            if (loaded.Base)
                break;
            Sleep(100);
        }
        if (!loaded.Base)
        {
            std::wcerr << L"[Error] LoadLibraryW exit code was 0x" << std::hex
                       << threadExitCode << std::dec
                       << L", but the DLL was not present in the target module list.\n";
            return InjectionResult::Failed;
        }

        std::wcout << L"[Info] Verified remote module base: 0x" << std::hex
                   << loaded.Base << std::dec << L"\n";
        return InjectionResult::Loaded;
    }

    bool ParseUnsigned(const wchar_t* text, unsigned long long& value)
    {
        if (!text || !*text || *text == L'-')
            return false;
        wchar_t* end = nullptr;
        errno = 0;
        const unsigned long long parsed = _wcstoui64(text, &end, 10);
        if (errno == ERANGE || !end || *end != L'\0')
            return false;
        value = parsed;
        return true;
    }

    void PrintUsage(const wchar_t* self)
    {
        std::wcout
            << L"Incursion DX12 loader\n\n"
            << L"Usage:\n"
            << L"  " << self << L"\n"
            << L"      Wait indefinitely for Test_C-Win64-Shipping.exe, then inject.\n\n"
            << L"  " << self << L" --timeout 300\n"
            << L"      Wait up to five minutes. Zero means no timeout.\n\n"
            << L"  " << self << L" --game \"C:\\...\\Test_C-Win64-Shipping.exe\"\n"
            << L"      Launch that executable if needed, wait for readiness, then inject.\n\n"
            << L"Options:\n"
            << L"  --dll <path>       Override the DLL beside Loader.exe.\n"
            << L"  --process <name>   Override the target executable name.\n"
            << L"  --timeout <sec>    Process wait timeout; 0 waits indefinitely.\n"
            << L"  --delay <sec>      Startup settle delay after modules load (default 2).\n"
            << L"  --no-wait          Fail immediately if the process is absent.\n"
            << L"  --wait             Explicitly enable the default wait behavior.\n"
            << L"  --help, -h         Show this help.\n\n"
            << L"By default IncursionCheat_DX12.dll is loaded from Loader.exe's folder.\n";
    }

    bool ParseArguments(int argc, wchar_t** argv, Options& options, bool& showHelp)
    {
        showHelp = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring argument = argv[i];
            if (argument == L"--help" || argument == L"-h")
            {
                showHelp = true;
                return true;
            }
            if (argument == L"--wait")
            {
                options.WaitForGame = true;
                continue;
            }
            if (argument == L"--no-wait")
            {
                options.WaitForGame = false;
                continue;
            }

            if ((argument == L"--dll" || argument == L"--game" ||
                 argument == L"--process" || argument == L"--timeout" ||
                 argument == L"--delay") && i + 1 < argc)
            {
                const wchar_t* value = argv[++i];
                if (argument == L"--dll") options.DllPath = value;
                else if (argument == L"--game") options.GamePath = value;
                else if (argument == L"--process")
                {
                    options.TargetExe = value;
                    options.TargetExeExplicit = true;
                    if (options.TargetExe.empty() ||
                        fs::path(options.TargetExe).filename().wstring() != options.TargetExe)
                    {
                        std::wcerr << L"[Error] --process expects an executable filename, not a path.\n";
                        return false;
                    }
                }
                else
                {
                    unsigned long long number = 0;
                    if (!ParseUnsigned(value, number))
                    {
                        std::wcerr << L"[Error] " << argument
                                   << L" expects a non-negative integer.\n";
                        return false;
                    }
                    if (argument == L"--timeout") options.TimeoutSeconds = number;
                    else
                    {
                        if (number > std::numeric_limits<unsigned long long>::max() / 1000ULL)
                            return false;
                        options.SettleMilliseconds = number * 1000ULL;
                    }
                }
                continue;
            }

            std::wcerr << L"[Error] Unknown or incomplete argument: " << argument << L"\n";
            return false;
        }
        return true;
    }
}

int wmain(int argc, wchar_t** argv)
{
    Options options{};
    options.DllPath = GetSelfDirectory() / L"IncursionCheat_DX12.dll";

    bool showHelp = false;
    if (!ParseArguments(argc, argv, options, showHelp))
    {
        PrintUsage(argv[0]);
        return 2;
    }
    if (showHelp)
    {
        PrintUsage(argv[0]);
        return 0;
    }

    if (!options.GamePath.empty() && !options.TargetExeExplicit)
    {
        const std::wstring launchedName = options.GamePath.filename().wstring();
        if (!launchedName.empty())
            options.TargetExe = launchedName;
    }

    options.DllPath = AbsoluteNormalized(options.DllPath);
    if (!ValidateDllImage(options.DllPath))
    {
        std::wcerr << L"[Hint] Build Release|x64; Loader.exe and IncursionCheat_DX12.dll share the same output folder.\n";
        return 3;
    }

    if (EnableDebugPrivilege())
        std::wcout << L"[Info] SeDebugPrivilege enabled.\n";

    const ULONGLONG waitStarted = GetTickCount64();
    DWORD processId = FindProcessId(options.TargetExe);
    if (!processId && !options.GamePath.empty())
    {
        options.GamePath = AbsoluteNormalized(options.GamePath);
        std::wcout << L"[Info] Target is absent; launching: " << options.GamePath << L"\n";
        if (!LaunchGame(options.GamePath))
            return 4;
    }

    processId = WaitForProcess(options, waitStarted);
    if (!processId)
    {
        if (options.WaitForGame && options.TimeoutSeconds)
            std::wcerr << L"[Error] Timed out waiting for " << options.TargetExe << L".\n";
        else
            std::wcerr << L"[Error] " << options.TargetExe << L" is not running.\n";
        return 4;
    }

    while (!WaitForProcessReady(processId, options))
    {
        if (!options.WaitForGame || TimeoutReached(waitStarted, options.TimeoutSeconds))
            return 5;
        std::wcout << L"[Wait] Target disappeared during startup; resuming process wait.\n";
        Sleep(ProcessPollMilliseconds);
        processId = WaitForProcess(options, waitStarted);
        if (!processId)
            return 5;
    }

    std::wcout << L"[Info] Target PID: " << processId << L"\n"
               << L"[Info] DLL: " << options.DllPath << L"\n"
               << L"[Info] Injecting with remote LoadLibraryW...\n";

    const InjectionResult result = InjectLibrary(processId, options.DllPath);
    if (result == InjectionResult::Failed)
    {
        std::wcerr << L"[Error] Injection failed.\n";
        return 6;
    }

    if (result == InjectionResult::AlreadyLoaded)
        std::wcout << L"[OK] Nothing to do; the DLL was already loaded.\n";
    else
        std::wcout << L"[OK] DLL injection completed and was verified.\n";
    std::wcout << L"[Info] HOME / INSERT / DELETE toggles the menu.\n";
    return 0;
}
