#pragma once

#include <string>

#include "web_helper.h"

namespace Web {

class WebSite {
public:
    explicit WebSite(WebTelemetryHelper& helper);

    std::string GetIndexHtml() const;
    std::string GetStyleCss() const;
    std::string GetAppJs() const;

    std::string GetCurrentSnapshotJson() const;
    std::string Get24HoursHistoryJson() const;
    std::string GetSessionHistoryJson() const;

private:
    static std::string EscapeJson(const std::string& value);

    static std::string BuildSnapshotJson(const Telemetry::Snapshot& snapshot);
    static std::string BuildAggregatedSeriesJson(const std::vector<Telemetry::AggregatedSnapshot>& series);
    static std::string BuildSessionHistoryJson(const std::vector<std::vector<Telemetry::AggregatedSnapshot>>& sessions);

    WebTelemetryHelper& helper_;
};

} // namespace Web
