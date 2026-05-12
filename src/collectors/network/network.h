#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Net
{
    struct InterfaceSnapshot {
        std::string name;
        std::string ipv4;
        double rxBytesPerSec = 0.0;
        double txBytesPerSec = 0.0;
        uint64_t rxTotalBytes = 0;
        uint64_t txTotalBytes = 0;
        bool isLoopback = false;
        bool isUp = false;
    };

    struct NetworkSnapshot {
        double totalRxPerSec = 0.0;
        double totalTxPerSec = 0.0;
        std::vector<InterfaceSnapshot> interfaces;
    };

    
    NetworkSnapshot GetSnapshot();
}