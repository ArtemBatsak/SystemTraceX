#pragma once

#include <vector>
#include <string>
#include <logger/logger.h>
#include "collectors/process/process.h"
#include <thread>
#include <atomic>



std::vector<int> internetTest(const std::vector<std::string>& hosts);
int ping(const std::string& host, int port = 443, int timeoutMs = 1000);


class TaskLogger
{
public:
    TaskLogger();
    ~TaskLogger();

public:
    void addProcessPerName(const std::string& name);
    void addProcessPerPid(uint32_t pid);

    void removeProcessPerName(const std::string& name);
    void removeProcessPerPid(uint32_t pid);

    void ProcessSnapshot();

private:
    struct WatchedProcess
    {
        std::string name;
        std::string path;
        bool enabled = true;
    };

    struct RuntimeProcessState
    {
        uint32_t pid = 0;

        std::string name;
        std::string path;

        double lastCpu = 0.0;
        size_t lastMemory = 0;

        bool alive = false;

        std::chrono::steady_clock::time_point lastSeen;
    };

    enum class ProcessEventType
    {
        Started,
        Exited,
        CpuSpike,
        MemorySpike,
        MonitoringEnabled,
        MonitoringDisabled
    };

private:
    void loadProcessNames();
    void saveProcesslist();

    void logProcessInfo(
        ProcessEventType type,
        const Proc::ProcessEntry& entry,
        const std::string& extra = ""
    );

    bool isWatchedProcess(const Proc::ProcessEntry& entry) const;

private:
    std::vector<WatchedProcess> processNamesToLog;

    std::unordered_map<uint32_t, RuntimeProcessState> runtimeProcesses_;

    Proc::ProcessSnapshot snapshot_;

    std::string processListPath_ = "Processlist.json";

private:
    constexpr static double CPU_SPIKE_THRESHOLD = 40.0;
    constexpr static size_t MEMORY_SPIKE_THRESHOLD_MB = 300;
private:
    std::atomic<bool> running_ = false;
    std::thread workerThread_;
};