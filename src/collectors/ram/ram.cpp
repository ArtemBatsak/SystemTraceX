#include "ram.h"

#ifdef _WIN32
#include <windows.h>
namespace Memory{

    MemorySnapshot GetSnapshot() {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);

        MemorySnapshot snap;
        snap.totalRAM = memInfo.ullTotalPhys;
        snap.freeRAM = memInfo.ullAvailPhys;
        snap.usedRAM = snap.totalRAM - snap.freeRAM;

        snap.commitLimit = memInfo.ullTotalPageFile;
        snap.commitUsed = memInfo.ullTotalPageFile - memInfo.ullAvailPageFile;

        return snap;
    }
}
#endif

#ifdef __linux__
#include <fstream>
#include <string>
#include <sstream>

namespace Memory
{
    MemorySnapshot GetSnapshot() {
        MemorySnapshot snap;
        std::ifstream file("/proc/meminfo");
        std::string line;

        uint64_t memFree = 0, buffers = 0, cached = 0, swapTotal = 0, swapFree = 0;

        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string key;
            uint64_t value;
			ss >> key >> value; // Value is in KB

            if (key == "MemTotal:")     snap.totalRAM = value * 1024;
            else if (key == "MemAvailable:") snap.freeRAM = value * 1024;
            else if (key == "SwapTotal:")    swapTotal = value * 1024;
            else if (key == "SwapFree:")     swapFree = value * 1024;
        }

        snap.usedRAM = snap.totalRAM - snap.freeRAM;
        snap.commitLimit = snap.totalRAM + swapTotal;
        snap.commitUsed = snap.usedRAM + (swapTotal - swapFree);

        return snap;
    }
    
}
#endif
