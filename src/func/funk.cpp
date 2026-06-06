

#include "funk.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>


#define NOMINMAX

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#pragma comment(lib, "ws2_32.lib")
#endif
#ifdef __linux__
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <chrono>
#include <string>
#endif 


Ping::Ping() {
	running_ = true;
	ping_avg = -1;
	get_hosts(filename);
    pingthread = std::thread([this]() {
        while (running_) {
            std::vector<std::string> hosts_snapshot;
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                hosts_snapshot = hosts_list;
            }

            for (size_t i = 0; i < hosts_snapshot.size(); ++i) {
                if (!running_) break;

                
                int result = ping(hosts_snapshot[i]);

                {
                    std::lock_guard<std::mutex> lock(data_mutex);

                    
                    if (i < ping_results.size()) {
                        ping_results[i].ping = result;
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock(get_mutex);
                ping_avg = take_ping_result();
            }
            {
                

            }

            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        });
}

Ping::~Ping() {
	running_ = false;
	if (pingthread.joinable()) {
		pingthread.join();
	}
}

#ifdef _WIN32
int Ping::ping(const std::string & host, int port, int timeoutMs)
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
            DWORD timeout = (DWORD)timeoutMs;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
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

int Ping::ping(const std::string& host, int port, int timeoutMs)
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

bool Ping::addhost(const std::string& host) { 
	if (host.size() >=5 ) return false;
    std::lock_guard<std::mutex> lock(data_mutex);
    hosts_list.push_back(host);
    HostResult new_res;
    new_res.host = host;
    new_res.ping = -1;
    ping_results.push_back(new_res);
	save_hosts(filename);
	return true;
}
bool Ping::removehost(const std::string& host) {
	std::lock_guard<std::mutex> lock(data_mutex);
	auto it = std::find(hosts_list.begin(), hosts_list.end(), host);
	if (it != hosts_list.end()) {
		size_t index = std::distance(hosts_list.begin(), it);
		hosts_list.erase(it);
		ping_results.erase(ping_results.begin() + index);
		save_hosts(filename);
		return true;
	}
	return false;
}

int Ping::take_ping_result() {
	std::lock_guard<std::mutex> lock(data_mutex);
	int total_ping = 0;
	int count = 0;
	for (const auto& res : ping_results) {
		if (res.ping >= 0) {
			total_ping += res.ping;
			count++;
		}
	}
	return count > 0 ? total_ping / count : -1;
}

void Ping::get_hosts(std::string filename) {
	std::ifstream file(filename);
	if (!file.is_open()) {
        std::ofstream outfile(filename);
        return;
	}
	std::string line;
	while (std::getline(file, line)) {
		if (!line.empty()) {
			addhost(line);
		}
	}
}

void Ping::save_hosts(std::string filename) {
	std::ofstream file(filename);
	if (!file.is_open()) {
		return;
	}
	for (const auto& host : hosts_list) {
		file << host << "\n";
	}
}





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
    const std::string& saveFile,
    const std::string& archiveDirectory
)
    : collector_(collector),
      saveFile_(saveFile),
      archiveDirectory_(archiveDirectory),
      running_(true)
{
    std::filesystem::create_directories(archiveDirectory_);
    loadTrackedProcesses();
	
}

TaskLogger::~TaskLogger() {

    running_ = false;

    if (loggerThread_.joinable()) {
        loggerThread_.join();
    }

    saveTrackedProcesses();
}
void TaskLogger::start() {
	loggerThread_ = std::thread(&TaskLogger::updateLoop, this);
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

    archiveWindow_.erase(path);
}

void TaskLogger::setTrackEnabled(const std::string& path,bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = trackedList_.find(path);

    if (it != trackedList_.end()) {
        it->second.enable = enable;

        if (!enable) {
            archiveWindow_.erase(path);
        }
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

std::vector<ArchivePoint> TaskLogger::getProcessArchiveByPath(const std::string& path) const {

    std::lock_guard<std::mutex> lock(mutex_);

    return readArchiveByPath(path);
}

std::vector<ArchivePoint> TaskLogger::getProcessArchiveByName(const std::string& name) const {

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& pair : trackedList_) {

        if (pair.second.name == name) {
            return readArchiveByPath(pair.second.path);
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

    auto nextTick =
        std::chrono::steady_clock::now();

    size_t archiveTicks = 0;
    uint64_t archiveWindowStartMs = 0;

    while (running_) {

        const auto tickStart =
            std::chrono::steady_clock::now();

        auto snapshot =
            collector_.GetLastProcessSnapshot();

        if (archiveTicks == 0) {
            archiveWindowStartMs = snapshot.timestampMs;
        }

        processSnapshot(snapshot);

        archiveTicks++;

        if (archiveTicks >= ARCHIVE_WINDOW_SIZE) {
            flushArchiveWindow(archiveWindowStartMs);
            archiveTicks = 0;
            archiveWindowStartMs = 0;
        }

        nextTick = tickStart + std::chrono::seconds(1);
        std::this_thread::sleep_until(nextTick);
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

        archiveWindow_[tracked.path].push_back(point); 
    }
}

void TaskLogger::flushArchiveWindow(uint64_t windowStartMs) {

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& pair : archiveWindow_) {

        auto& points = pair.second;

        if (points.empty()) {
            continue;
        }

        ArchivePoint archivePoint;
        archivePoint.timestamp =
            windowStartMs != 0 ? windowStartMs : points.front().timestamp;

        float cpuMin = std::numeric_limits<float>::max();
        float cpuMax = std::numeric_limits<float>::lowest();
        double cpuSum = 0.0;

        uint64_t ramMin = std::numeric_limits<uint64_t>::max();
        uint64_t ramMax = 0;
        uint64_t ramSum = 0;

        uint16_t instancesMin = std::numeric_limits<uint16_t>::max();
        uint16_t instancesMax = 0;
        uint64_t instancesSum = 0;

        for (const auto& point : points) {

            cpuMin = std::min(cpuMin, point.cpu);
            cpuMax = std::max(cpuMax, point.cpu);
            cpuSum += point.cpu;

            ramMin = std::min(ramMin, point.ram);
            ramMax = std::max(ramMax, point.ram);
            ramSum += point.ram;

            instancesMin = std::min(instancesMin, point.instances);
            instancesMax = std::max(instancesMax, point.instances);
            instancesSum += point.instances;
        }

        const auto count =
            static_cast<double>(points.size());

        archivePoint.cpuMin = cpuMin;
        archivePoint.cpuMax = cpuMax;
        archivePoint.cpuAvg =
            static_cast<float>(cpuSum / count);

        archivePoint.ramMin = ramMin;
        archivePoint.ramMax = ramMax;
        archivePoint.ramAvg =
            static_cast<uint64_t>(ramSum / points.size());

        archivePoint.instancesMin = instancesMin;
        archivePoint.instancesMax = instancesMax;
        archivePoint.instancesAvg =
            static_cast<float>(
                static_cast<double>(instancesSum) / count
            );

        appendArchivePoint(pair.first, archivePoint);
        points.clear();
    }
}

void TaskLogger::appendArchivePoint(const std::string& path, const ArchivePoint& point) {

    std::filesystem::create_directories(archiveDirectory_);

    const auto filePath =
        archiveFilePath(path);

    std::error_code ec;

    if (std::filesystem::exists(filePath, ec) && !ec) {

        const auto fileSize =
            std::filesystem::file_size(filePath, ec);
        // I don`t want check size of file, i will check count of records
		// it`s slowly then check size, but file will be not more than ~4.6 mb, so it`s not a problem

        if (!ec &&
			fileSize / sizeof(ArchivePoint) >= ARCHIVE_MAX_RECORDS) {   

            const auto oldPath =
                oldArchiveFilePath(path);

            std::filesystem::remove(oldPath, ec);
            ec.clear();

            std::filesystem::rename(filePath, oldPath, ec);
        }
    }

    std::ofstream out(
        filePath,
        std::ios::binary | std::ios::app
    );

    if (!out.is_open()) {
        return;
    }

    out.write(
        reinterpret_cast<const char*>(&point),
        sizeof(ArchivePoint)
    );
}

std::vector<ArchivePoint> TaskLogger::readArchiveByPath(const std::string& path) const {

    std::vector<ArchivePoint> records;

    auto readFile =
        [&records](const std::string& filePath) {

            std::ifstream in(filePath, std::ios::binary);

            if (!in.is_open()) {
                return;
            }

            ArchivePoint point;

            while (in.read(
                reinterpret_cast<char*>(&point),
                sizeof(ArchivePoint)
            )) {
                records.push_back(point);
            }
        };

    readFile(oldArchiveFilePath(path));
    readFile(archiveFilePath(path));

    return records;
}

std::string TaskLogger::archiveFilePath(const std::string& path) const {

    uint64_t hash = 1469598103934665603ull;

    for (unsigned char ch : path) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }

    std::ostringstream fileName;
    fileName
        << std::hex
        << std::setw(16)
        << std::setfill('0')
        << hash
        << ".bin";

    return (
        std::filesystem::path(archiveDirectory_) /
        fileName.str()
    ).string();
}

std::string TaskLogger::oldArchiveFilePath(const std::string& path) const {

    auto filePath =
        std::filesystem::path(archiveFilePath(path));

    filePath.replace_extension(".old.bin");

    return filePath.string();
}
