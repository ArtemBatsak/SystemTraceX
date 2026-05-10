#include "cpu.h"

#ifdef __linux__
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace CPU
{
    static CpuSnapshot cache;
    static std::chrono::steady_clock::time_point lastUpdate;
    static const int updateIntervalMs = 500;

    std::string GetCpuModel()
    {
        std::ifstream file("/proc/cpuinfo");
        std::string line;

        while (std::getline(file, line))
        {
            if (line.find("model name") != std::string::npos)
            {
                return line.substr(line.find(":") + 2);
            }
        }

        return "Unknown";
    }

    void Update()
    {
        auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastUpdate).count() < updateIntervalMs)
        {
            return;
        }

        struct CoreData
        {
            uint64_t idle = 0;
            uint64_t total = 0;
        };

        auto ReadStats = []()
        {
            std::ifstream file("/proc/stat");
            std::string line;
            std::vector<CoreData> data;

            while (std::getline(file, line))
            {
                if (line.rfind("cpu", 0) != 0)
                    continue;

                std::istringstream ss(line);

                std::string cpu;
                uint64_t user = 0, nice = 0, system = 0, idle = 0;
                uint64_t iowait = 0, irq = 0, softirq = 0, steal = 0;

                ss >> cpu >> user >> nice >> system >> idle
                    >> iowait >> irq >> softirq >> steal;

                uint64_t total =
                    user + nice + system + idle +
                    iowait + irq + softirq + steal;

                data.push_back({ idle, total });
            }

            return data;
        };

        auto first = ReadStats();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto second = ReadStats();

        if (first.empty() || second.empty())
        {
            cache.totalUsage = 0.0f;
            cache.perCoreUsage.clear();
            return;
        }

        uint64_t idleDiffTotal = second[0].idle - first[0].idle;
        uint64_t totalDiffTotal = second[0].total - first[0].total;

        cache.totalUsage = (totalDiffTotal == 0)
            ? 0.0f
            : 100.0f * (1.0f - (float)idleDiffTotal / (float)totalDiffTotal);

        cache.perCoreUsage.clear();

        for (size_t i = 1; i < first.size() && i < second.size(); ++i)
        {
            uint64_t idleDiff = second[i].idle - first[i].idle;
            uint64_t totalDiff = second[i].total - first[i].total;

            float usage = (totalDiff == 0)
                ? 0.0f
                : 100.0f * (1.0f - (float)idleDiff / (float)totalDiff);

            cache.perCoreUsage.push_back(usage);
        }

        lastUpdate = now;
    }

    float GetUsage()
    {
        Update();
        return cache.totalUsage;
    }

    int GetCoreCount()
    {
        return sysconf(_SC_NPROCESSORS_ONLN);
    }

    std::vector<float> GetPerCoreUsage()
    {
        Update();
        return cache.perCoreUsage;
    }
}
#endif

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

namespace CPU
{
    static CpuSnapshot cache;
    static std::chrono::steady_clock::time_point lastUpdate;
    static const int updateIntervalMs = 500;

    std::string GetCpuModel()
    {
        HKEY hKey;

        const char* path =
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";

        char buffer[256];
        DWORD size = sizeof(buffer);

        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            if (RegQueryValueExA(hKey, "ProcessorNameString", nullptr, nullptr,
                (LPBYTE)buffer, &size) == ERROR_SUCCESS)
            {
                RegCloseKey(hKey);
                return std::string(buffer);
            }

            RegCloseKey(hKey);
        }

        return "Unknown CPU";
    }

    void Update()
    {
        auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastUpdate).count() < updateIntervalMs)
        {
            return;
        }

        FILETIME idle1, kernel1, user1;
        FILETIME idle2, kernel2, user2;

        auto ToUint64 = [](FILETIME ft)
            {
                return ((uint64_t)ft.dwHighDateTime << 32)
                    | ft.dwLowDateTime;
            };

        GetSystemTimes(&idle1, &kernel1, &user1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        GetSystemTimes(&idle2, &kernel2, &user2);

        uint64_t idle = ToUint64(idle2) - ToUint64(idle1);
        uint64_t kernel = ToUint64(kernel2) - ToUint64(kernel1);
        uint64_t user = ToUint64(user2) - ToUint64(user1);
        uint64_t total = kernel + user;

        cache.totalUsage = (total == 0)
            ? 0.0f
            : 100.0f * (1.0f - ((float)idle / (float)total));

        cache.perCoreUsage.clear();

        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);

        DWORD coreCount = sysInfo.dwNumberOfProcessors;

        std::vector<PDH_HQUERY> q(coreCount);
        std::vector<PDH_HCOUNTER> c(coreCount);

        for (DWORD i = 0; i < coreCount; i++)
        {
            std::wstring path =
                L"\\Processor(" + std::to_wstring(i) + L")\\% Processor Time";

            PdhOpenQuery(NULL, 0, &q[i]);

            PdhAddEnglishCounterW(
                q[i],
                path.c_str(),
                0,
                &c[i]
            );

            PdhCollectQueryData(q[i]);
        }

        Sleep(500);

        for (DWORD i = 0; i < coreCount; i++)
        {
            PdhCollectQueryData(q[i]);

            PDH_FMT_COUNTERVALUE value;

            PdhGetFormattedCounterValue(
                c[i],
                PDH_FMT_DOUBLE,
                NULL,
                &value
            );

            cache.perCoreUsage.push_back((float)value.doubleValue);
        }

        for (DWORD i = 0; i < coreCount; i++)
        {
            PdhCloseQuery(q[i]);
        }

        lastUpdate = now;
    }

    float GetUsage()
    {
        Update();
        return cache.totalUsage;
    }

    int GetCoreCount()
    {
        SYSTEM_INFO sysInfo;

        GetSystemInfo(&sysInfo);

        return sysInfo.dwNumberOfProcessors;
    }

    std::vector<float> GetPerCoreUsage()
    {
        Update();
        return cache.perCoreUsage;
    }
}
#endif
