#include "disk.h"

#ifdef _WIN32
#include <pdh.h>
#include <vector>
#include <string>
#include <mutex>

#pragma comment(lib, "pdh.lib")

namespace Disk
{
    struct WinDisk
    {
        std::string name;

        PDH_HCOUNTER read;
        PDH_HCOUNTER write;
        PDH_HCOUNTER free;
    };

    static PDH_HQUERY query;
    static std::vector<WinDisk> disks;

    static DiskSystemSnapshot cache;
    static std::mutex mtx;
    static bool initialized = false;

    bool Init()
    {
        if (initialized) return true;

        if (PdhOpenQuery(NULL, 0, &query) != ERROR_SUCCESS)
            return false;

        disks.clear();

        WinDisk d;

        PdhAddEnglishCounterW(
            query,
            L"\\PhysicalDisk(*)\\Disk Read Bytes/sec",
            0,
            &d.read
        );

        PdhAddEnglishCounterW(
            query,
            L"\\PhysicalDisk(*)\\Disk Write Bytes/sec",
            0,
            &d.write
        );

        PdhAddEnglishCounterW(
            query,
            L"\\LogicalDisk(*)\\% Free Space",
            0,
            &d.free
        );

        disks.push_back(d);

        PdhCollectQueryData(query);

        initialized = true;
        return true;
    }

    void Update()
    {
        if (!Init()) return;

        PdhCollectQueryData(query);

        PDH_FMT_COUNTERVALUE readVal;
        PDH_FMT_COUNTERVALUE writeVal;

        PdhGetFormattedCounterValue(disks[0].read, PDH_FMT_DOUBLE, NULL, &readVal);
        PdhGetFormattedCounterValue(disks[0].write, PDH_FMT_DOUBLE, NULL, &writeVal);

        DiskSystemSnapshot snap;

        DiskSnapshot d;
        d.readBytesPerSec = readVal.doubleValue;
        d.writeBytesPerSec = writeVal.doubleValue;

        snap.disks.push_back(d);

        snap.totalReadPerSec = d.readBytesPerSec;
        snap.totalWritePerSec = d.writeBytesPerSec;

        {
            std::lock_guard<std::mutex> lock(mtx);
            cache = snap;
        }
    }

    DiskSystemSnapshot GetSnapshot()
    {
        Update();
        std::lock_guard<std::mutex> lock(mtx);
        return cache;
    }

    size_t GetDiskCount()
    {
        return disks.size();
    }
}

#endif
#ifdef __linux__

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>

namespace Disk
{
    struct Raw
    {
        uint64_t readSectors = 0;
        uint64_t writeSectors = 0;
    };

    static std::unordered_map<std::string, Raw> prev;
    static std::unordered_map<std::string, Raw> curr;

    static DiskSystemSnapshot cache;
    static std::mutex mtx;

    static void Read()
    {
        std::ifstream file("/proc/diskstats");
        std::string line;

        curr.clear();

        while (std::getline(file, line))
        {
            std::istringstream ss(line);

            int major, minor;
            std::string name;

            Raw r;

            ss >> major >> minor >> name;
            ss >> r.readSectors;

            for (int i = 0; i < 3; i++) ss >> std::ws >> std::string();

            ss >> r.writeSectors;

            curr[name] = r;
        }
    }

    void Update()
    {
        Read();

        DiskSystemSnapshot snap;

        for (auto& [name, c] : curr)
        {
            auto& p = prev[name];

            double read = c.readSectors - p.readSectors;
            double write = c.writeSectors - p.writeSectors;

            DiskSnapshot d;
            d.name = name;
            d.readBytesPerSec = read * 512;   // sector size
            d.writeBytesPerSec = write * 512;

            snap.disks.push_back(d);

            snap.totalReadPerSec += d.readBytesPerSec;
            snap.totalWritePerSec += d.writeBytesPerSec;
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            cache = snap;
        }

        prev = curr;
    }

    DiskSystemSnapshot GetSnapshot()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return cache;
    }

    size_t GetDiskCount()
    {
        return curr.size();
    }
}

#endif