#pragma once

#include <string>
#include <vector>

namespace CPU {

    struct CpuSnapshot
    {
        float totalUsage = 0.0f;
        std::vector<float> perCoreUsage;
    };

    // Name of the CPU model
    std::string GetCpuModel();

    // Refresh cached snapshot (contains sleep-based sampling for now)
    void Update();

    // Total CPU usage (%)
    float GetUsage();

    // Core count
    int GetCoreCount();

    // Usage per core (%)
    std::vector<float> GetPerCoreUsage();

}