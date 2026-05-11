#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Disk
{
    struct DiskSnapshot
    {
        std::string name;
        uint64_t freeBytes = 0;
        uint64_t totalBytes = 0;
    };

    struct DiskSystemSnapshot
    {
        std::vector<DiskSnapshot> disks;
    };

    DiskSystemSnapshot GetSnapshot();
}