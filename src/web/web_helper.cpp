#include "web_helper.h"

WebTelemetryHelper::WebTelemetryHelper(Telemetry::TelemetryCollector& collector)
    : collector_(collector) {}

Telemetry::Snapshot WebTelemetryHelper::GetCurrentSnapshot() {
    return collector_.GetLastSnapshot();
}

std::vector<Telemetry::Snapshot> WebTelemetryHelper::GetLiveGraphBootstrap() {
    return collector_.GetLiveWindow();
}

std::vector<Telemetry::AggregatedSnapshot> WebTelemetryHelper::Get24HoursSeries() {
    return collector_.GetRecent24Hours();
}

std::vector<Telemetry::AggregatedSnapshot> WebTelemetryHelper::GetLongRangeSeries() {
    return collector_.GetLongRange();
}

std::vector<std::vector<Telemetry::AggregatedSnapshot>> WebTelemetryHelper::GetSessionHistory() {
    auto all = collector_.GetLongRange();
    std::vector<std::vector<Telemetry::AggregatedSnapshot>> sessions;
    std::vector<Telemetry::AggregatedSnapshot> current;
    bool inSession = false;

    for (const auto& entry : all) {
        if (entry.recordType == Telemetry::AggregatedRecordType::SessionStart) {
            if (!current.empty()) {
                sessions.push_back(current);
                current.clear();
            }
            inSession = true;
            current.push_back(entry);
            continue;
        }

        if (entry.recordType == Telemetry::AggregatedRecordType::SessionEnd) {
            if (!inSession) {
                continue;
            }
            current.push_back(entry);
            sessions.push_back(current);
            current.clear();
            inSession = false;
            continue;
        }

        if (inSession) {
            current.push_back(entry);
        }
    }

    if (!current.empty()) {
        sessions.push_back(current);
    }

    return sessions;
}


