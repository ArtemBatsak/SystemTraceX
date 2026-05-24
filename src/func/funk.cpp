#include "funk.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#pragma comment(lib, "ws2_32.lib")

std::vector<int> internetTest(const std::vector<std::string>& hosts) {
    std::vector<int> results;
    for (const auto& host : hosts) {
        int latency = ping(host);
        results.push_back(latency);
    }
    return results;
}

int ping(const std::string & host, int port, int timeoutMs)
    {
        static bool init = false;
        if (!init)
        {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
                return -1;
            init = true;
        }

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
            return -1;

        int ping = -1;

        for (auto p = res; p; p = p->ai_next)
        {
            SOCKET sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock == INVALID_SOCKET)
                continue;

            auto start = std::chrono::high_resolution_clock::now();

            int result = connect(sock, p->ai_addr, (int)p->ai_addrlen);

            auto end = std::chrono::high_resolution_clock::now();

            if (result == 0)
            {
                ping = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                closesocket(sock);
                break;
            }

            closesocket(sock);
        }

        freeaddrinfo(res);
        return ping;
    }
#endif

#ifdef __linux__
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <chrono>
#include <string>

std::vector<int> internetTest(const std::vector<std::string>&hosts) {
        std::vector<int> results;
        for (const auto& host : hosts) {
            int latency = ping(host);
            results.push_back(latency);
        }
        return results;
    }

int ping(const std::string& host, int port, int timeoutMs)
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
            return -1;

        int ping = -1;

        for (auto p = res; p; p = p->ai_next)
        {
            int sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock < 0)
                continue;

            auto start = std::chrono::high_resolution_clock::now();

            int result = connect(sock, p->ai_addr, p->ai_addrlen);

            auto end = std::chrono::high_resolution_clock::now();

            if (result == 0)
            {
                ping = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                close(sock);
                break;
            }

            close(sock);
        }

        freeaddrinfo(res);
        return ping;
    }
#endif 


TaskLogger::TaskLogger()
{
    loadProcessNames();

    running_ = true;

    workerThread_ = std::thread([this]()
        {
            while (running_)
            {
                ProcessSnapshot();
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }
        });
}

TaskLogger::~TaskLogger()
{
    running_ = false;

    if (workerThread_.joinable())
        workerThread_.join();

    saveProcesslist();
}

void TaskLogger::loadProcessNames()
{
    namespace fs = std::filesystem;

    if (!fs::exists(processListPath_))
    {
        saveProcesslist();
        return;
    }

    std::ifstream file(processListPath_);

    if (!file.is_open())
    {
        spdlog::error("Failed open {}", processListPath_);
        return;
    }

    nlohmann::json j;
    file >> j;

    processNamesToLog.clear();

    if (!j.contains("processes"))
        return;

    for (const auto& item : j["processes"])
    {
        WatchedProcess proc;

        proc.name = item.value("name", "");
        proc.path = item.value("path", "");
        proc.enabled = item.value("enabled", true);

        if (!proc.name.empty())
        {
            processNamesToLog.push_back(proc);
        }
    }

    spdlog::info("Loaded {} watched processes", processNamesToLog.size());
}

void TaskLogger::saveProcesslist()
{
    nlohmann::json j;

    j["processes"] = nlohmann::json::array();

    for (const auto& proc : processNamesToLog)
    {
        j["processes"].push_back({
            { "name", proc.name },
            { "path", proc.path },
            { "enabled", proc.enabled }
            });
    }

    std::ofstream file(processListPath_);

    if (!file.is_open())
    {
        spdlog::error("Failed save {}", processListPath_);
        return;
    }

    file << j.dump(4);
}

void TaskLogger::addProcessPerName(const std::string& name)
{
    auto it = std::find_if(
        processNamesToLog.begin(),
        processNamesToLog.end(),
        [&](const WatchedProcess& p)
        {
            return p.name == name;
        }
    );

    if (it != processNamesToLog.end())
    {
        it->enabled = true;

        spdlog::info("Monitoring enabled for {}", name);

        saveProcesslist();
        return;
    }

    WatchedProcess proc;
    proc.name = name;
    proc.enabled = true;

    processNamesToLog.push_back(proc);

    spdlog::info("Added process monitoring {}", name);

    saveProcesslist();
}

void TaskLogger::addProcessPerPid(uint32_t pid)
{
    auto snap = Proc::GetSnapshot(0);

    auto it = std::find_if(
        snap.topProcesses.begin(),
        snap.topProcesses.end(),
        [&](const Proc::ProcessEntry& p)
        {
            return p.pid == pid;
        }
    );

    if (it == snap.topProcesses.end())
    {
        spdlog::warn("Process pid {} not found", pid);
        return;
    }

    auto already = std::find_if(
        processNamesToLog.begin(),
        processNamesToLog.end(),
        [&](const WatchedProcess& p)
        {
            return p.name == it->name &&
                p.path == it->path;
        }
    );

    if (already != processNamesToLog.end())
    {
        already->enabled = true;

        saveProcesslist();

        spdlog::info(
            "Monitoring enabled for {} ({})",
            it->name,
            it->path
        );

        return;
    }

    WatchedProcess proc;

    proc.name = it->name;
    proc.path = it->path;
    proc.enabled = true;

    processNamesToLog.push_back(proc);

    saveProcesslist();

    spdlog::info(
        "Added monitoring for {} ({})",
        proc.name,
        proc.path
    );
}

void TaskLogger::removeProcessPerName(const std::string& name)
{
    auto it = std::find_if(
        processNamesToLog.begin(),
        processNamesToLog.end(),
        [&](const WatchedProcess& p)
        {
            return p.name == name;
        }
    );

    if (it == processNamesToLog.end())
        return;

    it->enabled = false;

    spdlog::info("Monitoring disabled for {}", name);

    saveProcesslist();
}

void TaskLogger::removeProcessPerPid(uint32_t pid)
{
    auto runtime = runtimeProcesses_.find(pid);

    if (runtime == runtimeProcesses_.end())
        return;

    auto it = std::find_if(
        processNamesToLog.begin(),
        processNamesToLog.end(),
        [&](const WatchedProcess& p)
        {
            return p.name == runtime->second.name &&
                p.path == runtime->second.path;
        }
    );

    if (it == processNamesToLog.end())
        return;

    it->enabled = false;

    spdlog::info(
        "Monitoring disabled for {} ({})",
        it->name,
        it->path
    );

    saveProcesslist();
}

bool TaskLogger::isWatchedProcess(
    const Proc::ProcessEntry& entry
) const
{
    for (const auto& proc : processNamesToLog)
    {
        if (!proc.enabled)
            continue;

        if (proc.name != entry.name)
            continue;

        if (!proc.path.empty() &&
            proc.path != entry.path)
            continue;

        return true;
    }

    return false;
}

void TaskLogger::logProcessInfo(
    ProcessEventType type,
    const Proc::ProcessEntry& entry,
    const std::string& extra
)
{
    switch (type)
    {
    case ProcessEventType::Started:
    {
        spdlog::info(
            "[PROCESS_START] {} | pid={} | path={}",
            entry.name,
            entry.pid,
            entry.path
        );
        break;
    }

    case ProcessEventType::Exited:
    {
        spdlog::info(
            "[PROCESS_EXIT] {} | pid={} | path={}",
            entry.name,
            entry.pid,
            entry.path
        );
        break;
    }

    case ProcessEventType::CpuSpike:
    {
        spdlog::warn(
            "[CPU_SPIKE] {} | pid={} | cpu={:.2f}% | {}",
            entry.name,
            entry.pid,
            entry.cpuUsage,
            extra
        );
        break;
    }

    case ProcessEventType::MemorySpike:
    {
        double memMB =
            static_cast<double>(entry.memoryUsage) /
            (1024.0 * 1024.0);

        spdlog::warn(
            "[MEMORY_SPIKE] {} | pid={} | memory={:.2f} MB | {}",
            entry.name,
            entry.pid,
            memMB,
            extra
        );
        break;
    }

    default:
        break;
    }
}

void TaskLogger::ProcessSnapshot()
{
    snapshot_ = Proc::GetSnapshot(0);

    std::unordered_map<uint32_t, bool> currentPids;

    for (const auto& proc : snapshot_.topProcesses)
    {
        if (!isWatchedProcess(proc))
            continue;

        currentPids[proc.pid] = true;

        auto runtimeIt = runtimeProcesses_.find(proc.pid);

        if (runtimeIt == runtimeProcesses_.end())
        {
            RuntimeProcessState state;

            state.pid = proc.pid;
            state.name = proc.name;
            state.path = proc.path;

            state.lastCpu = proc.cpuUsage;
            state.lastMemory = proc.memoryUsage;

            state.alive = true;

            state.lastSeen =
                std::chrono::steady_clock::now();

            runtimeProcesses_[proc.pid] = state;

            logProcessInfo(
                ProcessEventType::Started,
                proc
            );

            continue;
        }

        auto& runtime = runtimeIt->second;

        double cpuDelta =
            std::abs(proc.cpuUsage - runtime.lastCpu);

        if (cpuDelta >= CPU_SPIKE_THRESHOLD)
        {
            logProcessInfo(
                ProcessEventType::CpuSpike,
                proc,
                "delta=" + std::to_string(cpuDelta)
            );
        }

        size_t memDelta = 0;

        if (proc.memoryUsage > runtime.lastMemory)
        {
            memDelta =
                proc.memoryUsage - runtime.lastMemory;
        }

        size_t memDeltaMb =
            memDelta / (1024 * 1024);

        if (memDeltaMb >= MEMORY_SPIKE_THRESHOLD_MB)
        {
            logProcessInfo(
                ProcessEventType::MemorySpike,
                proc,
                "delta_mb=" + std::to_string(memDeltaMb)
            );
        }

        runtime.lastCpu = proc.cpuUsage;
        runtime.lastMemory = proc.memoryUsage;

        runtime.lastSeen =
            std::chrono::steady_clock::now();
    }

    std::vector<uint32_t> deadProcesses;

    for (const auto& [pid, runtime] : runtimeProcesses_)
    {
        if (currentPids.contains(pid))
            continue;

        Proc::ProcessEntry deadEntry;

        deadEntry.pid = runtime.pid;
        deadEntry.name = runtime.name;
        deadEntry.path = runtime.path;

        logProcessInfo(
            ProcessEventType::Exited,
            deadEntry
        );

        deadProcesses.push_back(pid);
    }

    for (uint32_t pid : deadProcesses)
    {
        runtimeProcesses_.erase(pid);
    }
}