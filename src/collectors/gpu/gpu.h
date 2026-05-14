#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace GPU {

    enum class Vendor {
        Unknown,
        Nvidia,
        AMD,
        Intel
    };
    struct GpuInfo {
        std::string name;

        uint64_t vramTotalBytes = 0;
        uint64_t vramUsedBytes = 0;   // можно оставить 0 пока

        float usagePercent = 0.0f;     // можно оставить 0 пока

        Vendor vendor = Vendor::Unknown;

        bool isIntegrated = false;
        bool valid = false;
	};
    struct GpuSnapshot {

		int count = 0;
        std::vector<GpuInfo> gpus;

    };

    void Init();
	void Update();
    GpuSnapshot GetSnapshots();

}