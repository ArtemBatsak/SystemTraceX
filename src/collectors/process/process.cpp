#include "process.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace Process
{
    static ProcessSnapshot cache;
    static std::mutex mtx;

    struct RawProcess
    {
        uint64_t lastCpuTime = 0;
        uint64_t lastRead = 0;
        uint64_t lastWrite = 0;
    };

    static std::unordered_map<uint32_t, RawProcess> prev;
    static std::chrono::steady_clock::time_point lastUpdateTs;

    // ============================================================================
// WINDOWS IMPLEMENTATION
// ============================================================================
#ifdef _WIN32

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <thread>
#pragma comment(lib, "psapi.lib")

    void Update()
    {
        std::vector<ProcessInfo> processes;
        
        DWORD pids[4096], needed;
        if (!EnumProcesses(pids, sizeof(pids), &needed)) return;

        const int cpuCount = std::thread::hardware_concurrency();
        FILETIME idleTime, kernelTime, userTime;
        GetSystemTimes(&idleTime, &kernelTime, &userTime);
        ULARGE_INTEGER k, u;
        k.LowPart = kernelTime.dwLowDateTime; k.HighPart = kernelTime.dwHighDateTime;
        u.LowPart = userTime.dwLowDateTime;   u.HighPart = userTime.dwHighDateTime;
        const uint64_t totalNow = k.QuadPart + u.QuadPart;
        static uint64_t totalPrev = 0;
        const uint64_t totalDiff = (totalPrev == 0 || totalNow <= totalPrev) ? 0 : (totalNow - totalPrev);
        totalPrev = totalNow;

        int count = needed / sizeof(DWORD);
        for (int i = 0; i < count; i++)
        {
            DWORD pid = pids[i];
            if (!pid) continue;

            HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!h) continue;

            ProcessInfo p;
            p.pid = pid;

            char name[MAX_PATH] = { 0 };
            HMODULE mod;
            DWORD cb;
            if (EnumProcessModules(h, &mod, sizeof(mod), &cb)) GetModuleBaseNameA(h, mod, name, sizeof(name));
            p.name = name;

            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) p.memoryBytes = pmc.WorkingSetSize;

            FILETIME c, e, kf, uf;
            if (GetProcessTimes(h, &c, &e, &kf, &uf))
            {
                ULARGE_INTEGER pk, pu;
                pk.LowPart = kf.dwLowDateTime; pk.HighPart = kf.dwHighDateTime;
                pu.LowPart = uf.dwLowDateTime; pu.HighPart = uf.dwHighDateTime;
                const uint64_t procNow = pk.QuadPart + pu.QuadPart;
                auto &pp = prev[pid];
                if (pp.lastCpuTime > 0 && totalDiff > 0 && procNow >= pp.lastCpuTime)
                {
                    const uint64_t procDiff = procNow - pp.lastCpuTime;
                    p.cpuUsage = (100.0 * static_cast<double>(procDiff) / static_cast<double>(totalDiff)) * cpuCount;
                }
                pp.lastCpuTime = procNow;
            }
            CloseHandle(h);
            processes.push_back(p);
        }

        ProcessSnapshot snap;
        snap.processCount = static_cast<uint32_t>(processes.size());

        auto byCpu = processes;
        auto byMem = processes;
        auto byDisk = processes;

        std::sort(byCpu.begin(), byCpu.end(), [](const auto& a, const auto& b) { return a.cpuUsage > b.cpuUsage; });
        std::sort(byMem.begin(), byMem.end(), [](const auto& a, const auto& b) { return a.memoryBytes > b.memoryBytes; });
        std::sort(byDisk.begin(), byDisk.end(), [](const auto& a, const auto& b) {
            return (a.diskReadBytes + a.diskWriteBytes) > (b.diskReadBytes + b.diskWriteBytes);
        });

        size_t limit = std::min<size_t>(10, processes.size());
        snap.topCpu.assign(byCpu.begin(), byCpu.begin() + limit);
        snap.topMemory.assign(byMem.begin(), byMem.begin() + limit);
        snap.topDisk.assign(byDisk.begin(), byDisk.begin() + limit);
        snap.allProcesses = std::move(processes);

        std::lock_guard<std::mutex> lock(mtx);
        cache = std::move(snap);
    }

#endif // _WIN32

// ============================================================================
// LINUX IMPLEMENTATION
// ============================================================================
#ifdef __linux__

#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <thread>

    static uint64_t ReadTotalCpuJiffies()
    {
        std::ifstream file("/proc/stat");
        std::string cpu;
        uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        file >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }

    static uint64_t GetProcessCpuTime(int pid)
    {
        std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
        std::string line;
        std::getline(f, line);
        if (line.empty()) return 0;

        std::istringstream ss(line);
        std::string tmp;
        for (int i = 0; i < 13; i++) ss >> tmp;

        uint64_t utime = 0, stime = 0;
        ss >> utime >> stime;

        return utime + stime;
    }

    void Update()
    {
        std::vector<ProcessInfo> processes;
        
        const uint64_t totalCpuNow = ReadTotalCpuJiffies();
        static uint64_t totalCpuPrev = 0;
        const uint64_t totalCpuDiff = (totalCpuPrev == 0 || totalCpuNow <= totalCpuPrev) ? 0 : (totalCpuNow - totalCpuPrev);
        totalCpuPrev = totalCpuNow;

        DIR* dir = opendir("/proc");
        if (!dir) return;

        struct dirent* ent;
        while ((ent = readdir(dir)))
        {
            int pid = atoi(ent->d_name);
            if (pid <= 0) continue;

            ProcessInfo p;
            p.pid = static_cast<uint32_t>(pid);

            std::ifstream cmd("/proc/" + std::to_string(pid) + "/comm");
            std::getline(cmd, p.name);

            std::ifstream mem("/proc/" + std::to_string(pid) + "/status");
            std::string line;
            while (std::getline(mem, line))
            {
                if (line.find("VmRSS:") == 0)
                {
                    uint64_t kb = 0;
                    sscanf(line.c_str(), "VmRSS: %lu kB", &kb);
                    p.memoryBytes = kb * 1024;
                    break;
                }
            }

            uint64_t readBytes = 0, writeBytes = 0;
            std::ifstream io("/proc/" + std::to_string(pid) + "/io");
            while (std::getline(io, line))
            {
                if (line.rfind("read_bytes:", 0) == 0) sscanf(line.c_str(), "read_bytes: %lu", &readBytes);
                else if (line.rfind("write_bytes:", 0) == 0) sscanf(line.c_str(), "write_bytes: %lu", &writeBytes);
            }

            uint64_t cpuNow = GetProcessCpuTime(pid);
            auto& prevProc = prev[p.pid];
            if (prevProc.lastCpuTime > 0 && totalCpuDiff > 0 && cpuNow >= prevProc.lastCpuTime)
            {
                const uint64_t procDiff = cpuNow - prevProc.lastCpuTime;
                p.cpuUsage = 100.0 * static_cast<double>(procDiff) / static_cast<double>(totalCpuDiff);
            }
            prevProc.lastCpuTime = cpuNow;

            p.diskReadBytes = readBytes;
            p.diskWriteBytes = writeBytes;
            if (prevProc.lastRead > 0 && readBytes >= prevProc.lastRead) p.diskReadBytes = readBytes - prevProc.lastRead;
            if (prevProc.lastWrite > 0 && writeBytes >= prevProc.lastWrite) p.diskWriteBytes = writeBytes - prevProc.lastWrite;
            prevProc.lastRead = readBytes;
            prevProc.lastWrite = writeBytes;

            processes.push_back(p);
        }

        closedir(dir);

        ProcessSnapshot snap;
        snap.processCount = static_cast<uint32_t>(processes.size());

        auto byCpu = processes;
        auto byMem = processes;
        auto byDisk = processes;

        std::sort(byCpu.begin(), byCpu.end(), [](const auto& a, const auto& b) { return a.cpuUsage > b.cpuUsage; });
        std::sort(byMem.begin(), byMem.end(), [](const auto& a, const auto& b) { return a.memoryBytes > b.memoryBytes; });
        std::sort(byDisk.begin(), byDisk.end(), [](const auto& a, const auto& b) {
            return (a.diskReadBytes + a.diskWriteBytes) > (b.diskReadBytes + b.diskWriteBytes);
        });

        size_t limit = std::min<size_t>(10, processes.size());
        snap.topCpu.assign(byCpu.begin(), byCpu.begin() + limit);
        snap.topMemory.assign(byMem.begin(), byMem.begin() + limit);
        snap.topDisk.assign(byDisk.begin(), byDisk.begin() + limit);
        snap.allProcesses = std::move(processes);

        std::lock_guard<std::mutex> lock(mtx);
        cache = std::move(snap);
    }

#endif // __linux__

// ============================================================================
// COMMON INTERFACE
// ============================================================================

    ProcessSnapshot GetSnapshot()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return cache;
    }
}
