#include "cpu.h"

#ifdef __linux__
#include <fstream>

#include <filesystem>
#include <thread>
#include <sstream>
#include <unistd.h>
namespace fs = std::filesystem;
namespace CPU
{
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

    int GetCoreCount()
    {
        return sysconf(_SC_NPROCESSORS_ONLN);
    }

    float GetUsage()
    {
        auto ReadCPU = []()
            {
                std::ifstream file("/proc/stat");

                std::string cpu;

                uint64_t user, nice, system, idle;

                file >> cpu >> user >> nice >> system >> idle;

                return std::pair<uint64_t, uint64_t>(
                    idle,
                    user + nice + system + idle);
            };

        auto [idle1, total1] = ReadCPU();

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto [idle2, total2] = ReadCPU();

        uint64_t idle = idle2 - idle1;
        uint64_t total = total2 - total1;

        return 100.0f * (1.0f - ((float)idle / total));
    }

    std::vector<float> GetPerCoreUsage()
    {
        struct CoreData
        {
            uint64_t idle;
            uint64_t total;
        };

        auto ReadStats = []()
            {
                std::ifstream file("/proc/stat");

                std::string line;

                std::vector<CoreData> data;

                while (std::getline(file, line))
                {
                    
                    if (line.substr(0, 3) != "cpu")
                        continue;

                   
                    if (line.substr(0, 4) == "cpu ")
                        continue;

                    std::istringstream ss(line);

                    std::string cpu;

                    uint64_t user, nice, system, idle;
                    uint64_t iowait, irq, softirq, steal;

                    ss >> cpu
                        >> user
                        >> nice
                        >> system
                        >> idle
                        >> iowait
                        >> irq
                        >> softirq
                        >> steal;

                    uint64_t total =
                        user +
                        nice +
                        system +
                        idle +
                        iowait +
                        irq +
                        softirq +
                        steal;

                    data.push_back({ idle, total });
                }

                return data;
            };

        auto first = ReadStats();

        std::this_thread::sleep_for(
            std::chrono::milliseconds(500));

        auto second = ReadStats();

        std::vector<float> result;

        for (size_t i = 0; i < first.size(); i++)
        {
            uint64_t idleDiff =
                second[i].idle - first[i].idle;

            uint64_t totalDiff =
                second[i].total - first[i].total;

            float usage =
                100.0f *
                (1.0f - (float)idleDiff / totalDiff);

            result.push_back(usage);
        }

        return result;
    }
}
#endif

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <thread>
#include <pdh.h>
#include <vector>
#include <string>
#include <powrprof.h>

#pragma comment(lib, "PowrProf.lib")


namespace CPU{

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

    int GetCoreCount()
    {
        SYSTEM_INFO sysInfo;

        GetSystemInfo(&sysInfo);

        return sysInfo.dwNumberOfProcessors;
    }

    float GetUsage()
    {
        FILETIME idle1, kernel1, user1;
        FILETIME idle2, kernel2, user2;

        auto ToUint64 = [](FILETIME ft)
            {
                return ((uint64_t)ft.dwHighDateTime << 32)
                    | ft.dwLowDateTime;
            };

        GetSystemTimes(&idle1, &kernel1, &user1);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(500));

        GetSystemTimes(&idle2, &kernel2, &user2);

        uint64_t idle =
            ToUint64(idle2) - ToUint64(idle1);

        uint64_t kernel =
            ToUint64(kernel2) - ToUint64(kernel1);

        uint64_t user =
            ToUint64(user2) - ToUint64(user1);

        uint64_t total = kernel + user;

        return 100.0f * (1.0f - ((float)idle / total));
    }

    std::vector<float> GetPerCoreUsage()
    {
        std::vector<float> result;

        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);

        DWORD coreCount = sysInfo.dwNumberOfProcessors;

        std::vector<PDH_HQUERY> queries(coreCount);
        std::vector<PDH_HCOUNTER> counters(coreCount);

        PdhOpenQuery(NULL, 0, &queries[0]); 

        std::vector<PDH_HQUERY> q(coreCount);
        std::vector<PDH_HCOUNTER> c(coreCount);

        std::vector<double> values(coreCount);

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

            result.push_back((float)value.doubleValue);
        }

        for (DWORD i = 0; i < coreCount; i++)
        {
            PdhCloseQuery(q[i]);
        }

        return result;
    }

}
#endif