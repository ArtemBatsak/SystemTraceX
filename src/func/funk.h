#pragma once

#include <vector>
#include <string>
#include "collector/collector.h"
#include <thread>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <unordered_map>


std::vector<int> internetTest(const std::vector<std::string>& hosts);
int ping(const std::string& host, int port = 443, int timeoutMs = 1000);


struct MetricPoint {
    uint64_t timestamp = 0;
    float cpu = 0.0f;
    uint64_t ram = 0;
    uint16_t instances = 0;
};

struct ArchivePoint {
    uint64_t timestamp = 0;

    float cpuMin = 0.0f;
    float cpuMax = 0.0f;
    float cpuAvg = 0.0f;

    uint64_t ramMin = 0;
    uint64_t ramMax = 0;
    uint64_t ramAvg = 0;

    uint16_t instancesMin = 0;
    uint16_t instancesMax = 0;
    float instancesAvg = 0.0f;
};

constexpr size_t HISTORY_SIZE = 600;

struct ProcessRing {
    std::array<MetricPoint, HISTORY_SIZE> history{};
    size_t currentIndex = 0;
    size_t validEntries = 0;

    void addEntry(const MetricPoint& entry);

    MetricPoint getLatest() const;

    std::vector<MetricPoint> getAll() const;
};

struct TrackedProcess {
    std::string name = "";
    std::string path = "";
    bool enable = true;
};

class TaskLogger {
public:

    TaskLogger(
        Telemetry::TelemetryCollector& collector,
        const std::string& saveFile = "tracked_processes.txt",
        const std::string& archiveDirectory = "process_archive"
    );

    ~TaskLogger();

public:
	void start();
    void addProcessToTrack(
        const std::string& name,
        const std::string& path,
        bool enable = true
    );
    void removeProcessToTrack(const std::string& path);

    void setTrackEnabled(const std::string& path, bool enable);

public:

    std::vector<MetricPoint> getProcessHistoryByPath(const std::string& path) const;

    MetricPoint getLatestProcessPointByPath(const std::string& path) const;

    std::vector<MetricPoint> getProcessHistoryByName(const std::string& name) const;

    MetricPoint getLatestProcessPointByName(const std::string& name) const;

    std::vector<ArchivePoint> getProcessArchiveByPath(const std::string& path) const;

    std::vector<ArchivePoint> getProcessArchiveByName(const std::string& name) const;

public:

    void saveTrackedProcesses();

    void loadTrackedProcesses();

private:

    void updateLoop();

    void processSnapshot(const Proc::ProcessSnapshot& snapshot);

    void flushArchiveWindow(uint64_t windowStartMs);

    void appendArchivePoint(const std::string& path, const ArchivePoint& point);

    std::vector<ArchivePoint> readArchiveByPath(const std::string& path) const;

    std::string archiveFilePath(const std::string& path) const;

    std::string oldArchiveFilePath(const std::string& path) const;

private:

    static constexpr size_t ARCHIVE_WINDOW_SIZE = 30;
    static constexpr size_t ARCHIVE_MAX_RECORDS = 86400;

    Telemetry::TelemetryCollector& collector_;

    std::string saveFile_;

    std::string archiveDirectory_;

    std::unordered_map<std::string, TrackedProcess> trackedList_;

    std::unordered_map<std::string, ProcessRing> trackedProcesses_;

    std::unordered_map<std::string, std::vector<MetricPoint>> archiveWindow_;

    mutable std::mutex mutex_;

    std::thread loggerThread_;

    std::atomic<bool> running_;
};
