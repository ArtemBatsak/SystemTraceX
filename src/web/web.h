#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>

#include "httplib.h"
#include "web_helper.h"

// =========================
// Web server + telemetry UI
// =========================
class Web {
public:
    explicit Web(WebTelemetryHelper& helper);

    void Start(int port);

private:
    void SetupRoutes();

    // =========================
    // UI
    // =========================
    std::string GetIndexHtml() const;
    std::string GetStyleCss() const;
    std::string GetAppJs() const;

    // =========================
    // API JSON
    // =========================
    std::string GetCurrentSnapshotJson() const;
    std::string GetLiveHistoryJson() const;
    std::string Get24HoursHistoryJson() const;
    std::string GetLongHistoryJson() const;
    std::string GetSessionHistoryJson() const;

    // =========================
    // JSON builders
    // (они у тебя уже в cpp)
    // =========================
    std::string BuildSnapshotJson(const Telemetry::Snapshot& s) const;
    std::string BuildAggregatedSeriesJson(const std::vector<Telemetry::AggregatedSnapshot>& series) const;
    std::string BuildLiveSeriesJson(const std::vector<Telemetry::Snapshot>& series) const;
    std::string BuildSessionHistoryJson(const std::vector<std::vector<Telemetry::AggregatedSnapshot>>& sessions) const;

private:
    WebTelemetryHelper& helper_;

    httplib::Server svr_;

    // =========================
    // CACHE (shared with telemetry thread)
    // =========================
    struct Cache {
        Telemetry::Snapshot current;
        std::vector<Telemetry::Snapshot> live;
        std::vector<Telemetry::AggregatedSnapshot> h24;
        std::vector<Telemetry::AggregatedSnapshot> longSeries;
        std::vector<std::vector<Telemetry::AggregatedSnapshot>> sessions;
    };

    Cache cache_;
    mutable std::mutex cache_mtx_;
};