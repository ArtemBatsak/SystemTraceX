#pragma once

#include <string>
#include <vector>

namespace CPU {

    struct CpuSnapshot {
        float totalUsage = 0.0f;
        std::vector<float> perCoreUsage;
        int coreCount = 0;
        std::string cpuname;
    };
	void Init();
	void Update();
    CpuSnapshot GetSnapshot();

}
