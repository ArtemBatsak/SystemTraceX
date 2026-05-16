#pragma once

#include <string>
#include <sstream>
#include "httplib.h"
#include "web_helper.h" // твой Snapshot + helper
#include <nlohmann/json.hpp>
#include <fstream>
#include <memory>
#include <mutex>

using json = nlohmann::json;

class Web
{
public:
    explicit Web(WebTelemetryHelper& webHelper_);

    void Start(const std::string& host = "0.0.0.0", int port = 8080);
    void Stop();

private:
    std::string BuildSnapshotJson();
    std::string GetIndexHtml() const;
    std::string GetAppJs() const;
	std::string GetStylesCss() const;

private:
    WebTelemetryHelper& webHelper;

    std::unique_ptr<httplib::Server> svr;
};

static json BuildAggregatedSeriesJson(
    const std::vector<Telemetry::AggregatedSnapshot>& data);
static json BuildLiveSeriesJson(
    const std::vector<Telemetry::Snapshot>& data);
static json BuildSessionHistoryJson(
    const std::vector<std::vector<Telemetry::AggregatedSnapshot>>& sessions);

static std::string ReadFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file.is_open())
        return "";

    std::stringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}