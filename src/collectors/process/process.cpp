#include "process.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32

#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

#endif

#ifdef __linux__

#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

#endif

namespace Process
{
    static ProcessSnapshot cache;
    static std::mutex mtx;

    struct RawProcess
    {
        uint64_t lastCpuTime = 0;
        uint64_t lastSystemTime = 0;
        uint64_t lastRead = 0;
        uint64_t lastWrite = 0;
    };

    static std::unordered_map<uint32_t, RawProcess> prev;

    // =========================
    // COMMON UTILS
    // =========================

    static inline uint64_t now_ms()
    {
#ifdef _WIN32
        return GetTickCount64();
#else
        return 0; // optional improvement later
#endif
    }

    // =========================
    // WINDOWS CPU
    // =========================
#ifdef _WIN32

    static uint64_t FileTimeToU64(FILETIME ft)
    {
        ULARGE_INTEGER ui;
        ui.LowPart = ft.dwLowDateTime;
        ui.HighPart = ft.dwHighDateTime;
        return ui.QuadPart;
    }

    static double GetCpuUsage(uint32_t pid, FILETIME kernel, FILETIME user)
    {
        auto& p = prev[pid];

        uint64_t k = FileTimeToU64(kernel);
        uint64_t u = FileTimeToU64(user);

        uint64_t current = k + u;

        if (p.lastCpuTime == 0)
        {
            p.lastCpuTime = current;
            return 0.0;
        }

        uint64_t diff = current - p.lastCpuTime;
        p.lastCpuTime = current;

        // rough normalization (Task Manager style approximation)
        return (double)diff;
    }

#endif

    // =========================
    // LINUX CPU
    // =========================
#ifdef __linux__

    static uint64_t GetProcessCpuTime(int pid)
    {
        std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
        std::string line;
        std::getline(f, line);

        std::istringstream ss(line);
        std::string tmp;

        for (int i = 0; i < 13; i++) ss >> tmp;

        uint64_t utime, stime;
        ss >> utime >> stime;

        return utime + stime;
    }

#endif

    // =========================
    // UPDATE
    // =========================
    void Update()
    {
        std::vector<ProcessInfo> processes;

#ifdef _WIN32

        DWORD pids[4096], needed;

        if (!EnumProcesses(pids, sizeof(pids), &needed))
            return;

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

            if (EnumProcessModules(h, &mod, sizeof(mod), &cb))
                GetModuleBaseNameA(h, mod, name, sizeof(name));

            p.name = name;

            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
                p.memoryBytes = pmc.WorkingSetSize;

            FILETIME c, e, k, u;
            if (GetProcessTimes(h, &c, &e, &k, &u))
                p.cpuUsage = GetCpuUsage(pid, k, u);

            CloseHandle(h);

            processes.push_back(p);
        }

#endif

#ifdef __linux__

        DIR* dir = opendir("/proc");
        if (!dir) return;

        struct dirent* ent;

        while ((ent = readdir(dir)))
        {
            int pid = atoi(ent->d_name);
            if (pid <= 0) continue;

            ProcessInfo p;
            p.pid = pid;

            std::ifstream cmd("/proc/" + std::to_string(pid) + "/comm");
            std::getline(cmd, p.name);

            std::ifstream mem("/proc/" + std::to_string(pid) + "/status");
            std::string line;

            while (std::getline(mem, line))
            {
                if (line.find("VmRSS:") == 0)
                {
                    uint64_t kb;
                    sscanf(line.c_str(), "VmRSS: %lu kB", &kb);
                    p.memoryBytes = kb * 1024;
                    break;
                }
            }

            uint64_t cpuNow = GetProcessCpuTime(pid);
            auto& prevProc = prev[pid];

            if (prevProc.lastCpuTime == 0)
            {
                prevProc.lastCpuTime = cpuNow;
                p.cpuUsage = 0;
            }
            else
            {
                p.cpuUsage = (double)(cpuNow - prevProc.lastCpuTime);
                prevProc.lastCpuTime = cpuNow;
            }

            processes.push_back(p);
        }

        closedir(dir);

#endif

        // =========================
        // SNAPSHOT BUILD
        // =========================

        ProcessSnapshot snap;
        snap.processCount = (uint32_t)processes.size();

        auto byCpu = processes;
        auto byMem = processes;

        std::sort(byCpu.begin(), byCpu.end(),
            [](auto& a, auto& b) { return a.cpuUsage > b.cpuUsage; });

        std::sort(byMem.begin(), byMem.end(),
            [](auto& a, auto& b) { return a.memoryBytes > b.memoryBytes; });

        size_t limit = std::min<size_t>(10, processes.size());

        snap.topCpu.assign(byCpu.begin(), byCpu.begin() + limit);
        snap.topMemory.assign(byMem.begin(), byMem.begin() + limit);

        snap.allProcesses = std::move(processes);

        {
            std::lock_guard<std::mutex> lock(mtx);
            cache = std::move(snap);
        }
    }

    // =========================
    // GET SNAPSHOT
    // =========================
    ProcessSnapshot GetSnapshot()
    {
        Update();
        std::lock_guard<std::mutex> lock(mtx);
        return cache;
    }

    bool Init()
    {
        return true;
    }
}