#include "Memory.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>

namespace
{
    uintptr_t g_base = 0;
    size_t g_moduleSize = 0;

    enum class AccessRequirement { Read, Write, Execute };

    bool QueryRange(uintptr_t address, size_t size, AccessRequirement requirement)
    {
        if (!address || size == 0)
            return false;

        uintptr_t cursor = address;
        const uintptr_t end = address + size;
        if (end < address)
            return false;

        while (cursor < end)
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi))
                return false;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS))
                return false;

            const DWORD p = mbi.Protect & 0xFF;
            if (requirement == AccessRequirement::Write)
            {
                const bool writable = p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
                                      p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
                if (!writable)
                    return false;
            }
            else if (requirement == AccessRequirement::Execute)
            {
                const bool executable = p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
                                        p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
                if (!executable)
                    return false;
            }

            const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (regionEnd <= cursor)
                return false;
            cursor = std::min(regionEnd, end);
        }
        return true;
    }
}

Pattern::Pattern(const char* pattern)
{
    if (!pattern)
        return;

    std::string input(pattern);
    for (size_t i = 0; i < input.size();)
    {
        if (input[i] == ' ' || input[i] == '\t')
        {
            ++i;
            continue;
        }
        if (input[i] == '?')
        {
            bytes.push_back(0);
            mask.push_back('?');
            ++i;
            if (i < input.size() && input[i] == '?')
                ++i;
            continue;
        }
        if (i + 1 >= input.size())
            break;
        const std::string byteText = input.substr(i, 2);
        bytes.push_back(static_cast<uint8_t>(std::strtoul(byteText.c_str(), nullptr, 16)));
        mask.push_back('x');
        i += 2;
    }
}

bool Pattern::Match(const uint8_t* data, size_t size) const
{
    if (!data || bytes.empty() || bytes.size() > size)
        return false;
    for (size_t i = 0; i < bytes.size(); ++i)
        if (mask[i] == 'x' && data[i] != bytes[i])
            return false;
    return true;
}

bool Memory::Initialize(HMODULE module)
{
    if (!module)
        module = GetModuleHandleW(nullptr);
    if (!module)
        return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    g_base = reinterpret_cast<uintptr_t>(module);
    g_moduleSize = static_cast<size_t>(nt->OptionalHeader.SizeOfImage);
    return g_base != 0 && g_moduleSize != 0;
}

bool Memory::Attach(const wchar_t*)
{
    return Initialize();
}

uintptr_t Memory::GetBase() { return g_base; }
size_t Memory::GetModuleSize() { return g_moduleSize; }

uintptr_t Memory::ResolveRva(uintptr_t rva)
{
    if (!g_base && !Initialize())
        return 0;
    if (rva >= g_base && rva < g_base + g_moduleSize)
        return rva;
    return g_base + rva;
}

bool Memory::IsReadable(uintptr_t address, size_t size) { return QueryRange(address, size, AccessRequirement::Read); }
bool Memory::IsWritable(uintptr_t address, size_t size) { return QueryRange(address, size, AccessRequirement::Write); }
bool Memory::IsExecutable(uintptr_t address, size_t size) { return QueryRange(address, size, AccessRequirement::Execute); }

bool Memory::ReadRaw(uintptr_t address, void* out, size_t size)
{
    if (!out || !IsReadable(address, size))
        return false;
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
}

bool Memory::WriteRaw(uintptr_t address, const void* data, size_t size)
{
    if (!data || !IsWritable(address, size))
        return false;
    std::memcpy(reinterpret_cast<void*>(address), data, size);
    return true;
}

std::string Memory::ReadString(uintptr_t address, size_t maxLength)
{
    if (!address || maxLength == 0)
        return {};
    std::string out;
    out.reserve(maxLength);
    for (size_t i = 0; i < maxLength; ++i)
    {
        char c = 0;
        if (!ReadRaw(address + i, &c, sizeof(c)) || c == '\0')
            break;
        out.push_back(c);
    }
    return out;
}

std::wstring Memory::ReadWideString(uintptr_t address, size_t maxLength)
{
    if (!address || maxLength == 0)
        return {};
    std::wstring out;
    out.reserve(maxLength);
    for (size_t i = 0; i < maxLength; ++i)
    {
        wchar_t c = 0;
        if (!ReadRaw(address + i * sizeof(wchar_t), &c, sizeof(c)) || c == L'\0')
            break;
        out.push_back(c);
    }
    return out;
}

bool Memory::WriteString(uintptr_t address, const std::string& value)
{
    return WriteRaw(address, value.c_str(), value.size() + 1);
}

uintptr_t Memory::Alloc(size_t size, DWORD protect)
{
    return reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, protect));
}

void Memory::Free(uintptr_t address)
{
    if (address)
        VirtualFree(reinterpret_cast<void*>(address), 0, MEM_RELEASE);
}

uintptr_t Memory::Scan(const char* pattern, uintptr_t start, size_t size)
{
    if (!g_base && !Initialize())
        return 0;
    Pattern pat(pattern);
    if (pat.bytes.empty())
        return 0;
    if (!start)
        start = g_base;
    if (!size)
    {
        if (start < g_base || start >= g_base + g_moduleSize)
            return 0;
        size = (g_base + g_moduleSize) - start;
    }
    if (size < pat.bytes.size())
        return 0;

    const uintptr_t last = start + size - pat.bytes.size();
    for (uintptr_t p = start; p <= last; ++p)
    {
        if (!IsReadable(p, pat.bytes.size()))
            continue;
        if (pat.Match(reinterpret_cast<const uint8_t*>(p), pat.bytes.size()))
            return p;
    }
    return 0;
}

uintptr_t Memory::ScanModule(const char* pattern)
{
    return Scan(pattern, g_base, g_moduleSize);
}
