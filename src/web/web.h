#pragma once

#include <fstream>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "httplib.h"
#include "web_helper.h"
#include "../func/funk.h"

class Web {
public:
    Web(WebTelemetryHelper& webHelper_, TaskLogger& taskLogger_, Ping& ping_);

    void Start(const std::string& host = "0.0.0.0", int port = 8080);
    void Stop();

private:
    struct TrackedProcessView {
        std::string name;
        std::string path;
        bool enabled = true;
    };

    void LoadTrackedProcessesView();
    void UpsertTrackedProcessView(const std::string& name, const std::string& path, bool enabled);
    void RemoveTrackedProcessView(const std::string& path);
    std::string GetTrackedProcessesString(bool includeHistory);
    std::string GetTrackedLatestString();

    WebTelemetryHelper& webHelper;
    TaskLogger& taskLogger;
    std::vector<TrackedProcessView> trackedProcesses_;
    std::mutex trackedProcessesMutex_;
    std::unique_ptr<httplib::Server> svr;
};

static std::string ReadFile(const std::string& path) {
    namespace fs = std::filesystem;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
