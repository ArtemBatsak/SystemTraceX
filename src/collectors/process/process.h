#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Proc {
    struct ProcessEntry {
        uint32_t pid;
        std::string name;
        double cpuUsage = 0.0;
        uint64_t memoryUsage = 0;
        double importanceScore = 0.0; // Наш новый коэффициент
    };

    struct ProcessSnapshot {
        size_t totalProcesses = 0;
        std::vector<ProcessEntry> topProcesses; // Топ-20 по важности
    };

    ProcessSnapshot GetSnapshot();
}