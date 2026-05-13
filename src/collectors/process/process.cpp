#include "process.h"
#include <chrono>
#include <algorithm>
#include <map>
#include <vector>
#include <fstream>
#include <string>

namespace Proc {

    // Вспомогательная функция для получения времени в секундах
    static double GetCurrentSeconds() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now.time_since_epoch()).count();
    }
}
    // ==========================================================
    // WINDOWS IMPLEMENTATION
    // ==========================================================
#ifdef _WIN32  // ИСПРАВЛЕНО: макрос должен быть большими буквами
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

    ProcessSnapshot GetSnapshot() {
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

        size_t count = (std::min)((size_t)20, allProcesses.size());
        snap.topProcesses.assign(allProcesses.begin(), allProcesses.begin() + count);

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

        // Функция для получения общего времени CPU системы из /proc/stat
        unsigned long long GetTotalCpuTime() {
            std::ifstream file("/proc/stat");
            std::string cpu;
            unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
            if (!(file >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal)) return 0;
            return user + nice + system + idle + iowait + irq + softirq + steal;
        }

        ProcessSnapshot GetSnapshot() {
            ProcessSnapshot snap;
            std::vector<ProcessEntry> allProcesses;
            unsigned long long systemTime = GetTotalCpuTime();
            long pageSize = sysconf(_SC_PAGESIZE);

            DIR* dir = opendir("/proc");
            if (!dir) return snap;

            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                // Пропускаем всё, что не является папкой с PID
                if (!isdigit(entry->d_name[0])) continue;

                uint32_t pid = std::stoi(entry->d_name);
                ProcessEntry p;
                p.pid = pid;

                // 1. Получаем чистое имя процесса из /comm
                std::string commPath = "/proc/" + std::to_string(pid) + "/comm";
                std::ifstream commFile(commPath);
                if (commFile.is_open()) {
                    std::getline(commFile, p.name);
                }
                else {
                    continue; // Если не удалось прочитать, вероятно процесс уже закрыт
                }

                // 2. Получаем точную память из /statm (в страницах)
                std::string statmPath = "/proc/" + std::to_string(pid) + "/statm";
                std::ifstream statmFile(statmPath);
                unsigned long long dummy, resident;
                if (statmFile >> dummy >> resident) {
                    p.memoryUsage = resident * pageSize;
                }

                // 3. Получаем CPU тики из /stat
                std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
                std::ifstream statFile(statPath);
                if (statFile.is_open()) {
                    std::string tmp;
                    unsigned long long utime, stime;

                    // Пропускаем первые 13 полей до utime
                    // (поле имени в скобках может содержать пробелы, поэтому /stat надежно парсить сложно,
                    // но для числовых полей после 13-го это работает нормально)
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

                // 4. Считаем важность
                double memMB = (double)p.memoryUsage / (1024.0 * 1024.0);
                p.importanceScore = (p.cpuUsage * 1.5) + (memMB / 50.0);

                allProcesses.push_back(p);
            }
            closedir(dir);

            snap.totalProcesses = allProcesses.size();

            // Сортировка по убыванию важности
            std::sort(allProcesses.begin(), allProcesses.end(), [](const ProcessEntry& a, const ProcessEntry& b) {
                return a.importanceScore > b.importanceScore;
                });

            // Берем топ 20
            size_t count = (std::min)((size_t)20, allProcesses.size());
            snap.topProcesses.assign(allProcesses.begin(), allProcesses.begin() + count);

            // Периодическая очистка истории (чтобы не хранить данные о закрытых процессах)
            if (g_linuxCpuHistory.size() > 2000) {
                g_linuxCpuHistory.clear();
            }

            return snap;
        }
    }
#endif

