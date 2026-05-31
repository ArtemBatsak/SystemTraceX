#include "process.h"
#include <chrono>
#include <algorithm>
#include <map>
#include <vector>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Proc {

    // Helper function to get current time in seconds
    static double GetCurrentSeconds() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now.time_since_epoch()).count();
    }
}
    // ==========================================================
    // WINDOWS IMPLEMENTATION
    // ==========================================================
#ifdef _WIN32  // Windows macro is case-sensitive
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>


#pragma comment(lib, "psapi.lib")
namespace Proc {

    struct CpuHistory {
        uint64_t lastProcessTime = 0;
        uint64_t lastSystemTime = 0;
    };

    static std::unordered_map<uint32_t, CpuHistory> g_cpuHistory;

    static uint64_t FileTimeToUint64(const FILETIME& ft) {
        ULARGE_INTEGER ull;
        ull.LowPart = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;
        return ull.QuadPart;
    }

    ProcessSnapshot GetSnapshot(const int count_tasks) {
        ProcessSnapshot snap;
        std::vector<ProcessEntry> allProcesses;
        allProcesses.reserve(512);

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) {
            return snap;
        }

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);

        FILETIME idle, kernel, user;
        GetSystemTimes(&idle, &kernel, &user);

        uint64_t sysTime = FileTimeToUint64(kernel) + FileTimeToUint64(user);

        // Lambda to update CPU usage history. PID is correctly uint32_t.
        auto addHistory = [&](uint32_t pid, uint64_t procTime) {
            g_cpuHistory[pid] = { procTime, sysTime };
            };

        if (Process32FirstW(hSnap, &pe)) {
            do {
                uint32_t pid = pe.th32ProcessID;
                if (pid == 0) continue;

                ProcessEntry entry;
                entry.pid = pid;

                // Name retrieval (safe and fast)
                {
                    char buf[MAX_PATH];
                    int len = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, buf, MAX_PATH, nullptr, nullptr);
                    if (len > 1) {
                        entry.name.assign(buf, len - 1);
                    }
                }

                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (!hProc) {
                    allProcesses.push_back(entry);
                    continue;
                }

                // Memory and Path retrieval (Path is dynamically allocated on the heap to prevent stack overflow)
                {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                        entry.memoryUsage = pmc.WorkingSetSize;
                    }

                    // CRITICAL FIX: Use std::vector for path buffer instead of large static array on the stack.
                    std::vector<char> pathBuffer(32768);
                    DWORD size = static_cast<DWORD>(pathBuffer.size());

                    if (QueryFullProcessImageNameA(hProc, 0, pathBuffer.data(), &size)) {
                        entry.path.assign(pathBuffer.data(), size);
                    }
                }

                // CPU calculation
                FILETIME c, e, k, u;
                if (GetProcessTimes(hProc, &c, &e, &k, &u)) {

                    uint64_t procTime = FileTimeToUint64(k) + FileTimeToUint64(u);

                    auto it = g_cpuHistory.find(pid);

                    if (it != g_cpuHistory.end()) {
                        uint64_t procDiff = procTime - it->second.lastProcessTime;
                        uint64_t sysDiff = sysTime - it->second.lastSystemTime;

                        if (sysDiff > 0) {
                            entry.cpuUsage = static_cast<double>(procDiff) / static_cast<double>(sysDiff) * 100.0;
                        }
                        else {
                            // If time difference is zero, CPU usage cannot be calculated yet.
                            entry.cpuUsage = 0.0;
                        }
                    }
                    else {
                        // First collection cycle: set initial CPU usage to 0.
                        entry.cpuUsage = 0.0;
                    }

                    addHistory(pid, procTime);
                }

                CloseHandle(hProc);

                double memMB = entry.memoryUsage / (1024.0 * 1024.0);
                // Calculate Importance Score
                entry.importanceScore = entry.cpuUsage * 1.5 + static_cast<double>(memMB) / 50.0;

                allProcesses.push_back(std::move(entry)); // Use move for efficiency

            } while (Process32NextW(hSnap, &pe));
        }

        CloseHandle(hSnap);

        // Cleanup dead PIDs from history
        {
            std::unordered_set<uint32_t> alive;
            alive.reserve(allProcesses.size());

            for (const auto& p : allProcesses)
                alive.insert(p.pid);

            for (auto it = g_cpuHistory.begin(); it != g_cpuHistory.end(); ) {
                if (!alive.count(it->first))
                    it = g_cpuHistory.erase(it);
                else
                    ++it;
            }
        }

        snap.totalProcesses = allProcesses.size();

        // --- Optimized Sorting Logic ---
        auto compareFn = [](const ProcessEntry& a, const ProcessEntry& b) {
            return a.importanceScore > b.importanceScore; // Sort by score descending
            };

        if (count_tasks <= 0) {
            // If no limit is set, sort all processes entirely and move them to the snapshot.
            std::sort(allProcesses.begin(), allProcesses.end(), compareFn);
            snap.topProcesses = std::move(allProcesses);
        }
        else {
            // Partial sorting: only sort up to 'count_tasks' limit for fast retrieval of Top N.
            const size_t sortCount = (std::min)(static_cast<size_t>(count_tasks), allProcesses.size());

            std::partial_sort(
                allProcesses.begin(),
                allProcesses.begin() + sortCount,
                allProcesses.end(),
                compareFn
            );

            // Assign only the sorted Top N results to the snapshot.
            snap.topProcesses.assign(allProcesses.begin(), allProcesses.begin() + sortCount);
        }

        return snap;
    }



}
#endif
    // ==========================================================
    // LINUX IMPLEMENTATION
    // ==========================================================
#ifdef __linux__

#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>

namespace Proc {

    struct LinuxCpuHistory {
        unsigned long long utime;
        unsigned long long stime;
        unsigned long long totalTime;
    };

    static std::map<uint32_t, LinuxCpuHistory> g_linuxCpuHistory;

    // ================= CPU TOTAL TIME =================
    unsigned long long GetTotalCpuTime() {
        std::ifstream file("/proc/stat");

        std::string cpu;
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;

        if (!(file >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal))
            return 0;

        return user + nice + system + idle + iowait + irq + softirq + steal;
    }

    // ================= SAFE STAT PARSER =================
    static bool ParseStat(const std::string& path,
        unsigned long long& utime,
        unsigned long long& stime)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return false;

        std::string line;
        std::getline(file, line);

        size_t rparen = line.rfind(')');
        if (rparen == std::string::npos)
            return false;

        std::string rest = line.substr(rparen + 2);
        std::istringstream iss(rest);

        std::string state;
        iss >> state;

        unsigned long long tmp;

        for (int i = 0; i < 11; ++i)
            iss >> tmp;

        iss >> utime >> stime;

        return true;
    }

    // ================= MAIN SNAPSHOT =================
    ProcessSnapshot GetSnapshot(const int count_tasks)
    {
        ProcessSnapshot snap;
        std::vector<ProcessEntry> allProcesses;

        unsigned long long systemTime = GetTotalCpuTime();
        long pageSize = sysconf(_SC_PAGESIZE);

        DIR* dir = opendir("/proc");
        if (!dir)
            return snap;

        struct dirent* entry;

        while ((entry = readdir(dir)) != nullptr)
        {
            if (!isdigit(entry->d_name[0]))
                continue;

            uint32_t pid = std::stoi(entry->d_name);

            ProcessEntry p;
            p.pid = pid;

            // ================= NAME =================
            {
                std::ifstream f("/proc/" + std::to_string(pid) + "/comm");
                if (!f.is_open())
                    continue;

                std::getline(f, p.name);
            }

            // ================= PATH =================
            {
                std::string link = "/proc/" + std::to_string(pid) + "/exe";

                char buf[PATH_MAX];
                ssize_t len = readlink(link.c_str(), buf, sizeof(buf) - 1);

                if (len > 0) {
                    buf[len] = '\0';
                    p.path = buf;
                }
                else {
                    p.path = "";
                }
            }

            // ================= MEMORY =================
            {
                std::ifstream f("/proc/" + std::to_string(pid) + "/statm");

                unsigned long long dummy, resident;

                if (f >> dummy >> resident) {
                    p.memoryUsage = resident * pageSize;
                }
            }

            // ================= CPU =================
            {
                unsigned long long utime = 0, stime = 0;

                std::string statPath = "/proc/" + std::to_string(pid) + "/stat";

                if (ParseStat(statPath, utime, stime))
                {
                    unsigned long long procTime = utime + stime;

                    auto it = g_linuxCpuHistory.find(pid);

                    if (it != g_linuxCpuHistory.end())
                    {
                        unsigned long long prev =
                            it->second.utime + it->second.stime;

                        unsigned long long procDiff = procTime - prev;
                        unsigned long long sysDiff = systemTime - it->second.totalTime;

                        if (sysDiff > 0)
                        {
                            p.cpuUsage =
                                (100.0 * (double)procDiff) /
                                (double)sysDiff;
                        }
                    }

                    g_linuxCpuHistory[pid] = { utime, stime, systemTime };
                }
            }

            // ================= SCORE =================
            double memMB = (double)p.memoryUsage / (1024.0 * 1024.0);

            p.importanceScore =
                (p.cpuUsage * 1.5) +
                (memMB / 50.0);

            allProcesses.push_back(std::move(p));
        }

        closedir(dir);

        snap.totalProcesses = allProcesses.size();

        std::sort(allProcesses.begin(), allProcesses.end(),
            [](const ProcessEntry& a, const ProcessEntry& b)
            {
                return a.importanceScore > b.importanceScore;
            });

        if (count_tasks <= 0)
            snap.topProcesses = std::move(allProcesses);
        else
        {
            size_t count = std::min(
                (size_t)count_tasks,
                allProcesses.size());

            snap.topProcesses.assign(
                allProcesses.begin(),
                allProcesses.begin() + count);
        }

        if (g_linuxCpuHistory.size() > 2000)
            g_linuxCpuHistory.clear();

        return snap;
    }

} // namespace Proc

#endif