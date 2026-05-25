#include "process.h"
#include <chrono>
#include <algorithm>
#include <map>
#include <vector>
#include <fstream>
#include <string>

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

    static std::map<uint32_t, CpuHistory> g_cpuHistory;

    static uint64_t FileTimeToUint64(const FILETIME& ft) {
        ULARGE_INTEGER ull;
        ull.LowPart = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;
        return ull.QuadPart;
    }

    ProcessSnapshot GetSnapshot(const int count_tasks) {
        ProcessSnapshot snap;
        std::vector<ProcessEntry> allProcesses;

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return snap;

        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);

        FILETIME sysIdle, sysKernel, sysUser;
        GetSystemTimes(&sysIdle, &sysKernel, &sysUser);
        uint64_t currentSysTime = FileTimeToUint64(sysKernel) + FileTimeToUint64(sysUser);

        if (Process32FirstW(hSnap, &pe)) {
            do {
                ProcessEntry entry;
                entry.pid = pe.th32ProcessID;
                if (entry.pid == 0) continue;

                int size = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, NULL, 0, NULL, NULL);
                if (size > 0) {
                    entry.name.resize(size - 1);
                    WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, &entry.name[0], size, NULL, NULL);
                }

                HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.pid);
                if (hProc) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                        entry.memoryUsage = pmc.WorkingSetSize;
                        char pathBuffer[MAX_PATH];
                        DWORD size = MAX_PATH;

                        if (QueryFullProcessImageNameA(hProc, 0, pathBuffer, &size)) {
                            entry.path = std::string(pathBuffer, size);
                        }
                    }

                    FILETIME ftCreate, ftExit, ftKernel, ftUser;
                    if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                        uint64_t currentProcTime = FileTimeToUint64(ftKernel) + FileTimeToUint64(ftUser);
                        if (g_cpuHistory.count(entry.pid)) {
                            uint64_t procDiff = currentProcTime - g_cpuHistory[entry.pid].lastProcessTime;
                            uint64_t sysDiff = currentSysTime - g_cpuHistory[entry.pid].lastSystemTime;
                            if (sysDiff > 0) {
                                entry.cpuUsage = (100.0 * (double)procDiff) / (double)sysDiff;
                            }
                        }
                        g_cpuHistory[entry.pid] = { currentProcTime, currentSysTime };
                    }
                    CloseHandle(hProc);
                }

                double memMB = static_cast<double>(entry.memoryUsage) / (1024.0 * 1024.0);
                entry.importanceScore = (entry.cpuUsage * 1.5) + (memMB / 50.0);
                allProcesses.push_back(entry);
               
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);

        snap.totalProcesses = allProcesses.size();
        std::sort(allProcesses.begin(), allProcesses.end(), [](const ProcessEntry& a, const ProcessEntry& b) {
            return a.importanceScore > b.importanceScore;
            });

        if (count_tasks <= 0) {
            snap.topProcesses = allProcesses;
        }
        else{
            size_t count = (std::min)((size_t)count_tasks, allProcesses.size());
            snap.topProcesses.assign(allProcesses.begin(), allProcesses.begin() + count);
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
#include <sys/types.h>
#include <iostream>
namespace Proc {
        struct LinuxCpuHistory {
            unsigned long long utime;
            unsigned long long stime;
            unsigned long long totalTime;
        };

        static std::map<uint32_t, LinuxCpuHistory> g_linuxCpuHistory;

        // Returns total system CPU time from /proc/stat
        unsigned long long GetTotalCpuTime() {
            std::ifstream file("/proc/stat");
            std::string cpu;
            unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
            if (!(file >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal)) return 0;
            return user + nice + system + idle + iowait + irq + softirq + steal;
        }

        ProcessSnapshot GetSnapshot(const int count_tasks) {
            ProcessSnapshot snap;
            std::vector<ProcessEntry> allProcesses;
            unsigned long long systemTime = GetTotalCpuTime();
            long pageSize = sysconf(_SC_PAGESIZE);

            DIR* dir = opendir("/proc");
            if (!dir) return snap;

            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                // Skip entries that are not PID directories
                if (!isdigit(entry->d_name[0])) continue;

                uint32_t pid = std::stoi(entry->d_name);
                ProcessEntry p;
                p.pid = pid;

                // 1. Read process name from /comm
                std::string commPath = "/proc/" + std::to_string(pid) + "/comm";
                std::ifstream commFile(commPath);
                if (commFile.is_open()) {
                    std::getline(commFile, p.name);
                }
                else {
                    continue; // If unreadable, the process likely exited
                }

                // 2. Read resident memory from /statm (in pages)
                std::string statmPath = "/proc/" + std::to_string(pid) + "/statm";
                std::ifstream statmFile(statmPath);
                unsigned long long dummy, resident;
                if (statmFile >> dummy >> resident) {
                    p.memoryUsage = resident * pageSize;
                }

                // 3. Read CPU ticks from /stat
                std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
                std::ifstream statFile(statPath);
                if (statFile.is_open()) {
                    std::string tmp;
                    unsigned long long utime, stime;

                    // Skip the first 13 fields before utime
                    // (the process name field in parentheses may include spaces, so /stat parsing is tricky,
                    // but this approach is sufficient for numeric fields after the 13th one)
                    for (int i = 0; i < 13; ++i) statFile >> tmp;
                    if (statFile >> utime >> stime) {
                        unsigned long long procTime = utime + stime;
                        if (g_linuxCpuHistory.count(pid)) {
                            unsigned long long procDiff = procTime - (g_linuxCpuHistory[pid].utime + g_linuxCpuHistory[pid].stime);
                            unsigned long long sysDiff = systemTime - g_linuxCpuHistory[pid].totalTime;
                            if (sysDiff > 0) {
                                p.cpuUsage = (100.0 * (double)procDiff) / (double)sysDiff;
                            }
                        }
                        g_linuxCpuHistory[pid] = { utime, stime, systemTime };
                    }
                }

                // 4. Compute process importance score
                double memMB = (double)p.memoryUsage / (1024.0 * 1024.0);
                p.importanceScore = (p.cpuUsage * 1.5) + (memMB / 50.0);

                allProcesses.push_back(p);
            }
            closedir(dir);

            snap.totalProcesses = allProcesses.size();

            // Sort by descending importance
            std::sort(allProcesses.begin(), allProcesses.end(), [](const ProcessEntry& a, const ProcessEntry& b) {
                return a.importanceScore > b.importanceScore;
                });

            if (count_tasks <= 0) {
                snap.topProcesses = allProcesses;
            }
            else {
                size_t count = (std::min)((size_t)count_tasks, allProcesses.size());
                snap.topProcesses.assign(allProcesses.begin(), allProcesses.begin() + count);
            }

            // Periodically clear history to avoid keeping closed process entries
            if (g_linuxCpuHistory.size() > 2000) {
                g_linuxCpuHistory.clear();
            }

            return snap;
        }
    }
#endif
