#include "cpu.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <thread>
#include <chrono>
#include <pdh.h>
#include <vector>
#include <string>
#include <powrprof.h>

#pragma comment(lib, "PowrProf.lib")

#include <mutex>

#pragma comment(lib, "pdh.lib")

namespace CPU {

	// Static variables for PDH query and counters
    static PDH_HQUERY hQuery = nullptr;
    static PDH_HCOUNTER hTotalCounter;
    static std::vector<PDH_HCOUNTER> coreCounters;
    static CpuSnapshot currentCache;
    static std::mutex cacheMutex;

	// Helper function to fetch CPU model name from registry (work only 1 time, during initialization)
    std::string FetchCpuModel() {
        char buffer[256];
        DWORD size = sizeof(buffer);
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, "ProcessorNameString", nullptr, nullptr, (LPBYTE)buffer, &size);
            RegCloseKey(hKey);
            return std::string(buffer);
        }
        return "Unknown CPU";
    }

    void Init() {
        if (hQuery) return; 

        PdhOpenQueryW(NULL, 0, &hQuery);

		// Counter for total CPU usage
        PdhAddEnglishCounterW(hQuery, L"\\Processor(_Total)\\% Processor Time", 0, &hTotalCounter);

		// Counters for per-core usage
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        currentCache.coreCount = sysInfo.dwNumberOfProcessors;
        currentCache.cpuname = FetchCpuModel();
        coreCounters.resize(currentCache.coreCount);
        currentCache.perCoreUsage.resize(currentCache.coreCount);

        for (int i = 0; i < currentCache.coreCount; i++) {
            std::wstring path = L"\\Processor(" + std::to_wstring(i) + L")\\% Processor Time";
            PdhAddEnglishCounterW(hQuery, path.c_str(), 0, &coreCounters[i]);
        }

		// First data collection to initialize counters
        PdhCollectQueryData(hQuery);
    }

    void Update() {
        if (!hQuery) Init();

        if (PdhCollectQueryData(hQuery) == ERROR_SUCCESS) {
            std::lock_guard<std::mutex> lock(cacheMutex);

            PDH_FMT_COUNTERVALUE val;

			// Read total CPU usage
            PdhGetFormattedCounterValue(hTotalCounter, PDH_FMT_DOUBLE, NULL, &val);
            currentCache.totalUsage = (float)val.doubleValue;

			// Read per-core CPU usage
            for (int i = 0; i < currentCache.coreCount; i++) {
                PdhGetFormattedCounterValue(coreCounters[i], PDH_FMT_DOUBLE, NULL, &val);
                currentCache.perCoreUsage[i] = (float)val.doubleValue;
            }
        }
    }

    CpuSnapshot GetSnapshot() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        return currentCache; 
    }
}
#endif

#ifdef __linux__
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <mutex>
#include <algorithm>

namespace CPU {

    struct CpuTime {
        long long user, nice, system, idle, iowait, irq, softirq, steal;
        long long Total() const {
            return user + nice + system + idle + iowait + irq + softirq + steal;
        }
        long long Active() const {
            return user + nice + system + irq + softirq + steal;
        }
    };

    static CpuSnapshot currentCache;
    static std::vector<CpuTime> lastTimes; // Для хранения предыдущего замера
    static std::mutex cacheMutex;

    // Чтение имени процессора
    std::string FetchCpuModel() {
        std::ifstream file("/proc/cpuinfo");
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("model name") != std::string::npos) {
                return line.substr(line.find(":") + 2);
            }
        }
        return "Unknown CPU";
    }

	// Read CPU times from /proc/stat
    std::vector<CpuTime> ReadCpuTimes() {
        std::vector<CpuTime> times;
        std::ifstream file("/proc/stat");
        std::string line, label;

        while (std::getline(file, line)) {
			if (line.compare(0, 3, "cpu") == 0) { // Look for lines starting with "cpu"
                std::stringstream ss(line);
                ss >> label; 

                CpuTime t;
                ss >> t.user >> t.nice >> t.system >> t.idle >> t.iowait >> t.irq >> t.softirq >> t.steal;
                times.push_back(t);
            }
            else break;
        }
        return times;
    }

    void Init() {
        currentCache.cpuname = FetchCpuModel();
		lastTimes = ReadCpuTimes(); // First read to initialize
		currentCache.coreCount = lastTimes.size() - 1; // -1 because the first entry is total
        currentCache.perCoreUsage.resize(currentCache.coreCount);
    }

    void Update() {
        if (lastTimes.empty()) Init();

        auto newTimes = ReadCpuTimes();
        if (newTimes.size() != lastTimes.size()) return;

        std::lock_guard<std::mutex> lock(cacheMutex);

        for (size_t i = 0; i < newTimes.size(); ++i) {
            long long totalDiff = newTimes[i].Total() - lastTimes[i].Total();
            long long activeDiff = newTimes[i].Active() - lastTimes[i].Active();

            float usage = (totalDiff == 0) ? 0.0f : (100.0f * activeDiff / totalDiff);

            if (i == 0) {
                currentCache.totalUsage = usage; // Общая нагрузка
            }
            else {
                currentCache.perCoreUsage[i - 1] = usage; // Нагрузка по ядрам
            }
        }
        lastTimes = std::move(newTimes);
    }

    CpuSnapshot GetSnapshot() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        return currentCache;
    }
}
#endif
