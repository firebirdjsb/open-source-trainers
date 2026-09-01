#pragma once

#include <cstddef>

namespace DiagnosticsService
{
    // Writes a snapshot next to Test_C-Win64-Shipping.exe. This is intentionally
    // one-shot so it has zero steady-state cost while the menu is hooked.
    bool WriteFullDump(char* outPath, std::size_t outPathSize);
}
