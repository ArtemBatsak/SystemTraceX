#pragma once

#include <cstdint>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>

#include "../collector/collector.h"

class WebTelemetryHelper {
public:
    explicit WebTelemetryHelper(Telemetry::TelemetryCollector& collector);
    ~WebTelemetryHelper();

    // Background cache management
    void StartBackground();
    void StopBackground();

    // JSON getters - return prepared JSON caches ready to be served by Web
    nlohmann::json GetSnapshotJson();
    nlohmann::json GetLiveJson();
    nlohmann::json Get24HoursJson();
    nlohmann::json GetLongRangeJson();
    nlohmann::json GetSessionHistoryJson();

    Telemetry::Snapshot GetCurrentSnapshot();
    std::vector<Telemetry::Snapshot> GetLiveGraphBootstrap();
    std::vector<Telemetry::AggregatedSnapshot> Get24HoursSeries();
    std::vector<Telemetry::AggregatedSnapshot> GetLongRangeSeries();
    std::vector<std::vector<Telemetry::AggregatedSnapshot>> GetSessionHistory();


private:
    Telemetry::TelemetryCollector& collector_;
    // Caches and synchronization
    std::mutex cacheMutex_;
    nlohmann::json snapshotCache_;
    nlohmann::json liveCache_;
    nlohmann::json hours24Cache_;
    nlohmann::json longRangeCache_;
    nlohmann::json sessionsCache_;

    std::thread liveThread_;
    std::thread aggThread_;
    std::atomic<bool> running_ = false;
};
