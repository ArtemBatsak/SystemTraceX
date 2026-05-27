#include "funk.h"
#include "nlohmann/json.hpp"
#include <fstream>

using json = nlohmann::json;
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




void ProcessRing::addEntry(const MetricPoint& entry) {
    history[currentIndex] = entry;

    currentIndex =
        (currentIndex + 1) % HISTORY_SIZE;

    if (validEntries < HISTORY_SIZE) {
        validEntries++;
    }
}

MetricPoint ProcessRing::getLatest() const {

    if (validEntries == 0) {
        return {};
    }

    size_t latestIndex =
        (currentIndex + HISTORY_SIZE - 1)
        % HISTORY_SIZE;

    return history[latestIndex];
}

std::vector<MetricPoint> ProcessRing::getAll() const {

    std::vector<MetricPoint> result;

    result.reserve(validEntries);

    if (validEntries == 0) {
        return result;
    }

    size_t start =
        (currentIndex + HISTORY_SIZE - validEntries)
        % HISTORY_SIZE;

    for (size_t i = 0; i < validEntries; i++) {

        size_t index =
            (start + i) % HISTORY_SIZE;

        result.push_back(history[index]);
    }

    return result;
}

TaskLogger::TaskLogger(
    Telemetry::TelemetryCollector& collector,
    const std::string& saveFile
)
    : collector_(collector),
    saveFile_(saveFile),
    running_(true) {

    loadTrackedProcesses();

    loggerThread_ =
        std::thread(
            &TaskLogger::updateLoop,
            this
        );
}

TaskLogger::~TaskLogger() {

    running_ = false;

    if (loggerThread_.joinable()) {
        loggerThread_.join();
    }

    saveTrackedProcesses();
}

void TaskLogger::addProcessToTrack(
    const std::string& name,
    const std::string& path,
    bool enable
) {
    std::lock_guard<std::mutex> lock(mutex_);

    TrackedProcess proc;

    proc.name = name;
    proc.path = path;
    proc.enable = enable;

    trackedList_[path] = proc;

    if (!trackedProcesses_.count(path)) {
        trackedProcesses_[path] =
            ProcessRing{};
    }
}

void TaskLogger::removeProcessToTrack(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    trackedList_.erase(path);

    trackedProcesses_.erase(path);
}

void TaskLogger::setTrackEnabled(const std::string& path,bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = trackedList_.find(path);

    if (it != trackedList_.end()) {
        it->second.enable = enable;
    }
}

std::vector<MetricPoint>TaskLogger::getProcessHistoryByPath(const std::string& path) const {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it =
        trackedProcesses_.find(path);

    if (it == trackedProcesses_.end()) {
        return {};
    }

    return it->second.getAll();
}

MetricPoint TaskLogger::getLatestProcessPointByPath(const std::string& path) const {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it =
        trackedProcesses_.find(path);

    if (it == trackedProcesses_.end()) {
        return {};
    }

    return it->second.getLatest();
}

std::vector<MetricPoint>TaskLogger::getProcessHistoryByName(const std::string& name) const {

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& pair : trackedList_) {

        if (pair.second.name != name) {
            continue;
        }

        auto ringIt =
            trackedProcesses_.find(
                pair.second.path
            );

        if (ringIt != trackedProcesses_.end()) {
            return ringIt->second.getAll();
        }
    }

    return {};
}

MetricPoint TaskLogger::getLatestProcessPointByName(const std::string& name) const {

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& pair : trackedList_) {

        if (pair.second.name != name) {
            continue;
        }

        auto ringIt =
            trackedProcesses_.find(
                pair.second.path
            );

        if (ringIt != trackedProcesses_.end()) {
            return ringIt->second.getLatest();
        }
    }

    return {};
}

void TaskLogger::saveTrackedProcesses() {

    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream file(saveFile_);

    if (!file.is_open()) {
        return;
    }

    for (const auto& pair : trackedList_) {

        const auto& proc = pair.second;

        file
            << proc.name << "|"
            << proc.path << "|"
            << proc.enable
            << "\n";
    }

    file.close();
}

void TaskLogger::loadTrackedProcesses() {

    std::lock_guard<std::mutex> lock(mutex_);

    std::ifstream file(saveFile_);

    if (!file.is_open()) {
        return;
    }

    trackedList_.clear();

    std::string line;

    while (std::getline(file, line)) {

        size_t p1 = line.find('|');
        size_t p2 = line.rfind('|');

        if (p1 == std::string::npos ||
            p2 == std::string::npos ||
            p1 == p2) {
            continue;
        }

        TrackedProcess proc;

        proc.name =
            line.substr(0, p1);

        proc.path =
            line.substr(
                p1 + 1,
                p2 - p1 - 1
            );

        proc.enable =
            std::stoi(
                line.substr(p2 + 1)
            );

        trackedList_[proc.path] = proc;

        if (!trackedProcesses_.count(proc.path)) {
            trackedProcesses_[proc.path] =
                ProcessRing{};
        }
    }

    file.close();
}

void TaskLogger::updateLoop() {

    while (running_) {

        auto snapshot =
            collector_.GetLastProcessSnapshot();

        processSnapshot(snapshot);

        std::this_thread::sleep_for(
            std::chrono::seconds(1)
        );
    }
}

void TaskLogger::processSnapshot(const Proc::ProcessSnapshot& snapshot) {

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& trackedPair : trackedList_) {

        const auto& tracked =
            trackedPair.second;

        if (!tracked.enable) {
            continue;
        }

        double totalCpu = 0.0;
        uint64_t totalRam = 0;
        uint16_t instances = 0;

        for (const auto& proc : snapshot.topProcesses) {

            if (proc.path != tracked.path) {
                continue;
            }

            totalCpu += proc.cpuUsage;
            totalRam += proc.memoryUsage;

            instances++;
        }

        if (instances == 0) {
            continue;
        }

        MetricPoint point;

        point.timestamp = snapshot.timestampMs;

        point.cpu = static_cast<float>(totalCpu);

        point.ram = totalRam;

        point.instances = instances;

        trackedProcesses_[tracked.path].addEntry(point);
    }
}