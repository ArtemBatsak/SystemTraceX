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
    Web(WebTelemetryHelper& webHelper_, TaskLogger& taskLogger_);

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

    fs::path resolvedPath = path;
    std::error_code ec;
    fs::path cursor = fs::current_path(ec);

    if (!ec) {
        while (true) {
            const fs::path sourcePath = cursor / "src" / path;
            if (fs::exists(sourcePath, ec) && !ec) {
                resolvedPath = sourcePath;
                break;
            }

            const fs::path parent = cursor.parent_path();
            if (parent == cursor) {
                break;
            }
            cursor = parent;
        }
    }

    if (resolvedPath == path && !fs::exists(resolvedPath, ec)) {
        ec.clear();
        cursor = fs::current_path(ec);

        if (!ec) {
            while (true) {
                const fs::path bundledPath = cursor / path;
                if (fs::exists(bundledPath, ec) && !ec) {
                    resolvedPath = bundledPath;
                    break;
                }

                const fs::path parent = cursor.parent_path();
                if (parent == cursor) {
                    break;
                }
                cursor = parent;
            }
        }
    }

    std::ifstream file(resolvedPath, std::ios::binary);
    if (!file.is_open()) return "";

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
