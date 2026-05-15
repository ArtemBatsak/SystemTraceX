#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "web_helper.h"

struct WebResponse {
    int status = 200;
    std::string contentType;
    std::string body;
};

class Web {
public:
    explicit Web(WebTelemetryHelper& helper);

    using RouteHandler = std::function<WebResponse()>;

    void RegisterRoute(const std::string& path, RouteHandler handler);
    void RegisterDefaultRoutes();
    WebResponse HandleRequest(const std::string& path) const;

private:
    std::string GetIndexHtml() const;
    std::string GetStyleCss() const;
    std::string GetAppJs() const;

    std::string GetCurrentSnapshotJson() const;
    std::string Get24HoursHistoryJson() const;
    std::string GetSessionHistoryJson() const;
    std::string GetLiveHistoryJson() const;
    std::string GetLongHistoryJson() const;

    static std::string EscapeJson(const std::string& value);
    static std::string BuildSnapshotJson(const Telemetry::Snapshot& snapshot);
    static std::string BuildAggregatedSeriesJson(const std::vector<Telemetry::AggregatedSnapshot>& series);
    static std::string BuildLiveSeriesJson(const std::vector<Telemetry::Snapshot>& series);
    static std::string BuildSessionHistoryJson(const std::vector<std::vector<Telemetry::AggregatedSnapshot>>& sessions);

    WebTelemetryHelper& helper_;
    std::unordered_map<std::string, RouteHandler> routes_;
};
