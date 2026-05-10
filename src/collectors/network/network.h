#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Net
{
    struct InterfaceSnapshot
    {
        std::string name;

        double rxBytesPerSec = 0.0;
        double txBytesPerSec = 0.0;

        uint64_t rxTotalBytes = 0;
        uint64_t txTotalBytes = 0;

        bool isLoopback = false;
        bool isVirtual = false;
        bool isUp = false;
    };

    struct NetworkSnapshot
    {
        double totalRxPerSec = 0.0;
        double totalTxPerSec = 0.0;

        std::vector<InterfaceSnapshot> interfaces;

        bool hasInternet = false; // optional heuristic
    };

    // INIT (once)
    bool Init();

    // UPDATE (background thread or manual tick)
    void Update();

    // SNAPSHOT (fast getter)
    NetworkSnapshot GetSnapshot();

    // helpers
    size_t GetAdapterCount();
}