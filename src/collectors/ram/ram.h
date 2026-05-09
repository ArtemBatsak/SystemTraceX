#pragma once

#include <cstdint>

namespace Memory
{
    struct MemorySnapshot
    {
        uint64_t totalRAM = 0;
        uint64_t usedRAM = 0;
        uint64_t freeRAM = 0;

        uint64_t commitLimit = 0; // "swap total" (Windows)
        uint64_t commitUsed = 0;  // "swap used"
    };

    
    void Update();

    // RAM
    uint64_t GetTotalRAM();
    uint64_t GetUsedRAM();
    uint64_t GetFreeRAM();

    // "Swap" (Commit system)
    uint64_t GetTotalSwap();
    uint64_t GetUsedSwap();
    float GetSwapUsagePercent();
}

