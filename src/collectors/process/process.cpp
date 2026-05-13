#include "process.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <map>
#include <algorithm>

#pragma comment(lib, "psapi.lib")

namespace Proc {

    struct CpuHistory {
        uint64_t lastProcessTime = 0;
        uint64_t lastSystemTime = 0;
    };

    // Храним историю времен для каждого процесса
    static std::map<uint32_t, CpuHistory> g_cpuHistory;

    // Вспомогательная функция для перевода FILETIME в uint64_t
    static uint64_t FileTimeToUint64(const FILETIME& ft) {
        ULARGE_INTEGER ull;
        ull.LowPart = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;
        return ull.QuadPart;
    }

    ProcessSnapshot GetSnapshot() {
        ProcessSnapshot snap;
        std::vector<ProcessEntry> allProcesses;

        // 1. Получаем системное время и снимок (как раньше)
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

                // Конвертация имени
                int size = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, NULL, 0, NULL, NULL);
                if (size > 0) {
                    entry.name.resize(size - 1);
                    WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, &entry.name[0], size, NULL, NULL);
                }

                HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.pid);
                if (hProc) {
                    // ПАМЯТЬ
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                        entry.memoryUsage = pmc.WorkingSetSize;
                    }

                    // CPU
                    FILETIME ftCreate, ftExit, ftKernel, ftUser;
                    if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                        uint64_t currentProcTime = FileTimeToUint64(ftKernel) + FileTimeToUint64(ftUser);
                        if (g_cpuHistory.count(entry.pid)) {
                            uint64_t procDiff = currentProcTime - g_cpuHistory[entry.pid].lastProcessTime;
                            uint64_t sysDiff = currentSysTime - g_cpuHistory[entry.pid].lastSystemTime;
                            if (sysDiff > 0) {
                                entry.cpuUsage = (100.0 * procDiff) / sysDiff;
                            }
                        }
                        g_cpuHistory[entry.pid] = { currentProcTime, currentSysTime };
                    }
                    CloseHandle(hProc);
                }

                // 2. ВЫЧИСЛЯЕМ КОЭФФИЦИЕНТ ВАЖНОСТИ
                // CPU важнее: 1% = 1 балл. 
                // RAM: 100MB = 1 балл.
                double memMB = static_cast<double>(entry.memoryUsage) / (1024.0 * 1024.0);
                entry.importanceScore = (entry.cpuUsage * 1.5) + (memMB / 50.0);
                // Тут я чуть подкрутил веса: CPU в 1.5 раза важнее, а 1 балл даем за каждые 50MB

                allProcesses.push_back(entry);
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);

        snap.totalProcesses = allProcesses.size();

        // 3. СОРТИРУЕМ ПО ВАЖНОСТИ
        std::sort(allProcesses.begin(), allProcesses.end(), [](const ProcessEntry& a, const ProcessEntry& b) {
            return a.importanceScore > b.importanceScore;
            });

        // Берем топ-20
        size_t count = (std::min)((size_t)20, allProcesses.size());
        snap.topProcesses.assign(allProcesses.begin(), allProcesses.begin() + count);

        return snap;
    }

} 