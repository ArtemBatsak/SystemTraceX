#pragma once

#include <string>
#include <vector>

namespace CPU {

	// Name of the CPU model
    std::string GetCpuModel();

	// Total CPU usage (%)
    float GetUsage();

	// Core count
    int GetCoreCount();

	// Usage per core (%)
    std::vector<float> GetPerCoreUsage();

}
   
