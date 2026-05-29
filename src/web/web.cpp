#include "web.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

namespace {
constexpr const char* kTrackedProcessesFile = "tracked_processes.txt";

std::string BoolJson(bool value) {
    return value ? "true" : "false";
}

std::string ShortPath(const std::string& path) {
    constexpr size_t maxLength = 58;
    if (path.size() <= maxLength) {
        return path;
    }

    const auto slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return path.substr(0, 24) + "..." + path.substr(path.size() - 24);
    }

    const std::string fileName = path.substr(slash + 1);
    const std::string root = path.size() > 2 && path[1] == ':' ? path.substr(0, 3) : path.substr(0, 1);
    const char separator = path[slash];
    return root + "..." + separator + fileName;
}

int ParseProcessCount(const httplib::Request& req, int fallback) {
    if (!req.has_param("count")) {
        return fallback;
    }

    try {
        const int count = std::stoi(req.get_param_value("count"));
        return std::clamp(count, 0, 2000);
    }
    catch (...) {
        return fallback;
    }
}

json ParseBodyJson(const httplib::Request& req) {
    if (req.body.empty()) {
        return json::object();
    }

    auto payload = json::parse(req.body, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        return json::object();
    }

    return payload;
}

std::string LastPathPart(const std::string& path) {
    const auto slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return path;
    }

    return path.substr(slash + 1);
}

void AppendMetricPoint(std::ostringstream& ss, const MetricPoint& point) {
    ss << "{"
        << "\"timestamp\":" << point.timestamp << ","
        << "\"cpu\":" << point.cpu << ","
        << "\"ram\":" << point.ram << ","
        << "\"instances\":" << point.instances
        << "}";
}
}

Web::Web(WebTelemetryHelper& webHelper_, TaskLogger& taskLogger_)
    : webHelper(webHelper_),
      taskLogger(taskLogger_) {
    svr = std::make_unique<httplib::Server>();
    LoadTrackedProcessesView();
}

void Web::LoadTrackedProcessesView() {
    std::lock_guard<std::mutex> lock(trackedProcessesMutex_);

    trackedProcesses_.clear();

    std::ifstream file(kTrackedProcessesFile);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        const size_t first = line.find('|');
        const size_t last = line.rfind('|');

        if (first == std::string::npos || last == std::string::npos || first == last) {
            continue;
        }

        TrackedProcessView process;
        process.name = line.substr(0, first);
        process.path = line.substr(first + 1, last - first - 1);
        process.enabled = line.substr(last + 1) != "0";

        if (!process.path.empty()) {
            trackedProcesses_.push_back(std::move(process));
        }
    }
}

void Web::UpsertTrackedProcessView(const std::string& name, const std::string& path, bool enabled) {
    std::lock_guard<std::mutex> lock(trackedProcessesMutex_);

    for (auto& process : trackedProcesses_) {
        if (process.path == path) {
            if (!name.empty()) {
                process.name = name;
            }
            process.enabled = enabled;
            return;
        }
    }

    trackedProcesses_.push_back({ name.empty() ? LastPathPart(path) : name, path, enabled });
}

void Web::RemoveTrackedProcessView(const std::string& path) {
    std::lock_guard<std::mutex> lock(trackedProcessesMutex_);

    trackedProcesses_.erase(
        std::remove_if(
            trackedProcesses_.begin(),
            trackedProcesses_.end(),
            [&](const TrackedProcessView& process) {
                return process.path == path;
            }),
        trackedProcesses_.end());
}

std::string Web::GetTrackedProcessesString(bool includeHistory) {
    std::vector<TrackedProcessView> processes;
    {
        std::lock_guard<std::mutex> lock(trackedProcessesMutex_);
        processes = trackedProcesses_;
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "{\n  \"tracked\": [\n";

    for (size_t i = 0; i < processes.size(); ++i) {
        const auto& process = processes[i];
        const auto latest = taskLogger.getLatestProcessPointByPath(process.path);

        ss << "    {\n"
            << "      \"name\": \"" << EscapeJsonString(process.name) << "\",\n"
            << "      \"path\": \"" << EscapeJsonString(process.path) << "\",\n"
            << "      \"shortPath\": \"" << EscapeJsonString(ShortPath(process.path)) << "\",\n"
            << "      \"enabled\": " << BoolJson(process.enabled) << ",\n"
            << "      \"latest\": ";
        AppendMetricPoint(ss, latest);

        if (includeHistory) {
            const auto history = taskLogger.getProcessHistoryByPath(process.path);
            ss << ",\n      \"history\": [\n";
            for (size_t j = 0; j < history.size(); ++j) {
                ss << "        ";
                AppendMetricPoint(ss, history[j]);
                ss << (j + 1 < history.size() ? "," : "") << "\n";
            }
            ss << "      ]\n";
        }
        else {
            ss << "\n";
        }

        ss << "    }" << (i + 1 < processes.size() ? "," : "") << "\n";
    }

    ss << "  ]\n}";
    return ss.str();
}

std::string Web::GetTrackedLatestString() {
    std::vector<TrackedProcessView> processes;
    {
        std::lock_guard<std::mutex> lock(trackedProcessesMutex_);
        processes = trackedProcesses_;
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "{\n  \"points\": [\n";

    for (size_t i = 0; i < processes.size(); ++i) {
        const auto& process = processes[i];
        const auto latest = taskLogger.getLatestProcessPointByPath(process.path);

        ss << "    {\n"
            << "      \"path\": \"" << EscapeJsonString(process.path) << "\",\n"
            << "      \"latest\": ";
        AppendMetricPoint(ss, latest);
        ss << "\n    }" << (i + 1 < processes.size() ? "," : "") << "\n";
    }

    ss << "  ]\n}";
    return ss.str();
}

void Web::Start(const std::string& host, int port) {
    svr->Get("/", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(ReadFile("web/index.html"), "text/html; charset=utf-8");
    });
    svr->Get("/tasks", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(ReadFile("web/taskmanager.html"), "text/html; charset=utf-8");
    });
    svr->Get("/logs", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(ReadFile("web/logs.html"), "text/html; charset=utf-8");
    });

    svr->Get("/script.js", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(ReadFile("web/script.js"), "application/javascript; charset=utf-8");
    });
    svr->Get("/taskmanager.js", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(ReadFile("web/taskmanager.js"), "application/javascript; charset=utf-8");
    });

    svr->Get("/api/snapshot", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(webHelper.GetSnapshotString(), "application/json");
    });
    svr->Get("/api/telemetry/live", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(webHelper.GetLiveWindowString(), "application/json");
    });
    svr->Get("/api/telemetry/24h", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(webHelper.GetAggregatedWindowString("24h"), "application/json");
    });
    svr->Get("/api/telemetry/long", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(webHelper.GetAggregatedWindowString("long"), "application/json");
    });
    svr->Get("/api/processes", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_content(webHelper.GetProcessesString(ParseProcessCount(req, 40)), "application/json");
    });
    svr->Get("/api/processes/all", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(webHelper.GetProcessesString(0), "application/json");
    });

    svr->Get("/api/task-logger/tracked", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetTrackedProcessesString(false), "application/json");
    });
    svr->Get("/api/task-logger/history", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetTrackedProcessesString(true), "application/json");
    });
    svr->Get("/api/task-logger/latest", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetTrackedLatestString(), "application/json");
    });

    svr->Post("/api/task-logger/watch", [this](const httplib::Request& req, httplib::Response& res) {
        const auto payload = ParseBodyJson(req);
        std::string name = payload.value("name", "");
        std::string path = payload.value("path", "");
        const bool enabled = payload.value("enabled", true);

        if (path.empty() && req.has_param("path")) {
            path = req.get_param_value("path");
        }
        if (name.empty() && req.has_param("name")) {
            name = req.get_param_value("name");
        }

        if (path.empty()) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"path is required\"}", "application/json");
            return;
        }

        if (name.empty()) {
            name = LastPathPart(path);
        }

        taskLogger.addProcessToTrack(name, path, enabled);
        taskLogger.saveTrackedProcesses();
        UpsertTrackedProcessView(name, path, enabled);

        res.set_content("{\"ok\":true}", "application/json");
    });

    svr->Post("/api/task-logger/enabled", [this](const httplib::Request& req, httplib::Response& res) {
        const auto payload = ParseBodyJson(req);
        std::string path = payload.value("path", "");
        const bool enabled = payload.value("enabled", true);

        if (path.empty() && req.has_param("path")) {
            path = req.get_param_value("path");
        }

        if (path.empty()) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"path is required\"}", "application/json");
            return;
        }

        taskLogger.setTrackEnabled(path, enabled);
        taskLogger.saveTrackedProcesses();
        UpsertTrackedProcessView("", path, enabled);

        res.set_content("{\"ok\":true}", "application/json");
    });

    svr->Post("/api/task-logger/remove", [this](const httplib::Request& req, httplib::Response& res) {
        const auto payload = ParseBodyJson(req);
        std::string path = payload.value("path", "");

        if (path.empty() && req.has_param("path")) {
            path = req.get_param_value("path");
        }

        if (path.empty()) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"path is required\"}", "application/json");
            return;
        }

        taskLogger.removeProcessToTrack(path);
        taskLogger.saveTrackedProcesses();
        RemoveTrackedProcessView(path);

        res.set_content("{\"ok\":true}", "application/json");
    });

    svr->listen(host.c_str(), port);
}

void Web::Stop() { svr->stop(); }
