#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Disk
{
    struct DiskSnapshot
    {
        std::string name;

        uint64_t readBytesTotal = 0;
        uint64_t writeBytesTotal = 0;

        double readBytesPerSec = 0.0;
        double writeBytesPerSec = 0.0;

        double usagePercent = 0.0;
        uint64_t freeBytes = 0;
        uint64_t totalBytes = 0;
    };

    struct DiskSystemSnapshot
    {
        std::vector<DiskSnapshot> disks;

        double totalReadPerSec = 0.0;
        double totalWritePerSec = 0.0;
    };

    bool Init();
    void Update();
    DiskSystemSnapshot GetSnapshot();

    size_t GetDiskCount();
}