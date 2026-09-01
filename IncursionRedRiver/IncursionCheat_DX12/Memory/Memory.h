#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

struct Pattern
{
    std::vector<uint8_t> bytes;
    std::string mask;
    explicit Pattern(const char* pattern);
    bool Match(const uint8_t* data, size_t size) const;
};

namespace Memory
{
    bool Initialize(HMODULE module = nullptr);

    // Backward-compatible name. This is an INTERNAL DLL now, so processName is ignored.
    bool Attach(const wchar_t* processName = nullptr);

    uintptr_t GetBase();
    size_t GetModuleSize();
    uintptr_t ResolveRva(uintptr_t rva);

    bool IsReadable(uintptr_t address, size_t size = 1);
    bool IsWritable(uintptr_t address, size_t size = 1);
    bool IsExecutable(uintptr_t address, size_t size = 1);
    bool ReadRaw(uintptr_t address, void* out, size_t size);
    bool WriteRaw(uintptr_t address, const void* data, size_t size);

    template <typename T>
    T Read(uintptr_t address)
    {
        static_assert(std::is_trivially_copyable_v<T>, "Memory::Read requires trivially copyable T");
        T value{};
        ReadRaw(address, &value, sizeof(T));
        return value;
    }

    template <typename T>
    bool TryRead(uintptr_t address, T& out)
    {
        static_assert(std::is_trivially_copyable_v<T>, "Memory::TryRead requires trivially copyable T");
        return ReadRaw(address, &out, sizeof(T));
    }

    template <typename T>
    bool Write(uintptr_t address, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "Memory::Write requires trivially copyable T");
        return WriteRaw(address, &value, sizeof(T));
    }

    std::string ReadString(uintptr_t address, size_t maxLength = 256);
    std::wstring ReadWideString(uintptr_t address, size_t maxLength = 256);
    bool WriteString(uintptr_t address, const std::string& value);

    uintptr_t Alloc(size_t size, DWORD protect = PAGE_READWRITE);
    void Free(uintptr_t address);

    uintptr_t Scan(const char* pattern, uintptr_t start = 0, size_t size = 0);
    uintptr_t ScanModule(const char* pattern);
}
