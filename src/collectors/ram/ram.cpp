#include "ram.h"

#ifdef _WIN32
#include <windows.h>
#include <chrono>
namespace Memory{

    static MemorySnapshot cache;

    static std::chrono::steady_clock::time_point lastUpdate;
    static const int updateIntervalMs = 500;

    void Update()
    {
        auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastUpdate).count() < updateIntervalMs)
        {
            return;
        }

        MEMORYSTATUSEX state;
        state.dwLength = sizeof(state);

        GlobalMemoryStatusEx(&state);

        cache.totalRAM = state.ullTotalPhys;
        cache.freeRAM = state.ullAvailPhys;
        cache.usedRAM = state.ullTotalPhys - state.ullAvailPhys;

        cache.commitLimit = state.ullTotalPageFile;
        cache.commitUsed = state.ullTotalPageFile - state.ullAvailPageFile;

        lastUpdate = now;
    }

    // ---------------- RAM ----------------

    uint64_t GetTotalRAM()
    {
        Update();
        return cache.totalRAM;
    }

    uint64_t GetUsedRAM()
    {
        Update();
        return cache.usedRAM;
    }

    uint64_t GetFreeRAM()
    {
        Update();
        return cache.freeRAM;
    }

    // ---------------- Swap (Commit) ----------------

    uint64_t GetTotalSwap()
    {
        Update();
        return cache.commitLimit;
    }

    uint64_t GetUsedSwap()
    {
        Update();
        return cache.commitUsed;
    }

    float GetSwapUsagePercent()
    {
        Update();

        if (cache.commitLimit == 0)
            return 0.0f;

        return (float)cache.commitUsed * 100.0f /
            (float)cache.commitLimit;
    }

    MemorySnapshot GetSnapshot()
    {
        Update();
        return cache;
    }
}
#endif

#ifdef __linux__
#include <fstream>
#include <string>

namespace Memory
{
    static uint64_t ReadMemValue(const std::string& key)
    {
        std::ifstream file("/proc/meminfo");
        std::string line;

        while (std::getline(file, line))
        {
            if (line.find(key) != std::string::npos)
            {
                size_t pos = line.find(":");
                std::string value = line.substr(pos + 1);

                return std::stoull(value) * 1024; // KB -> bytes
            }
        }

        return 0;
    }

    // ---------------- RAM ----------------

    uint64_t GetTotalRAM()
    {
        return ReadMemValue("MemTotal");
    }

    uint64_t GetFreeRAM()
    {
        return ReadMemValue("MemAvailable");
    }

    uint64_t GetUsedRAM()
    {
        return GetTotalRAM() - GetFreeRAM();
    }

    // ---------------- SWAP ----------------

    uint64_t GetTotalSwap()
    {
        return ReadMemValue("SwapTotal");
    }

    uint64_t GetUsedSwap()
    {
        uint64_t total = ReadMemValue("SwapTotal");
        uint64_t free = ReadMemValue("SwapFree");

        return total - free;
    }

    float GetSwapUsagePercent()
    {
        uint64_t total = GetTotalSwap();

        if (total == 0)
            return 0.0f;

        return (float)GetUsedSwap() * 100.0f / (float)total;
    }

    MemorySnapshot GetSnapshot()
    {
        MemorySnapshot snapshot;
        snapshot.totalRAM = GetTotalRAM();
        snapshot.freeRAM = GetFreeRAM();
        snapshot.usedRAM = snapshot.totalRAM - snapshot.freeRAM;
        snapshot.commitLimit = GetTotalSwap();
        snapshot.commitUsed = GetUsedSwap();
        return snapshot;
    }
}
#endif
