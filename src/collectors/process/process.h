#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Process
{
    struct ProcessInfo
    {
        uint32_t pid = 0;
        std::string name;

        double cpuUsage = 0.0;          // %
        uint64_t memoryBytes = 0;

        uint64_t diskReadBytes = 0;
        uint64_t diskWriteBytes = 0;

        uint64_t netRxBytes = 0;
        uint64_t netTxBytes = 0;
    };

    struct ProcessSnapshot
    {
        uint32_t processCount = 0;

        std::vector<ProcessInfo> topCpu;
        std::vector<ProcessInfo> topMemory;
        std::vector<ProcessInfo> topDisk;
        std::vector<ProcessInfo> topNetwork;

        std::vector<ProcessInfo> allProcesses;
    };

    void Update();
    ProcessSnapshot GetSnapshot();
}