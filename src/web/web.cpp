#include "web.h"

#include "../logger/logger.h"

Web::Web(WebTelemetryHelper& webHelper_, TaskLogger* taskLogger)
    : webHelper(webHelper_), taskLogger_(taskLogger) {
    svr = std::make_unique<httplib::Server>();
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
    svr->Get("/api/processes", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(webHelper.GetProcessesString(), "application/json");
    });
    svr->Get("/api/errors", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(webHelper.GetErrorsString(), "application/json");
    });
    svr->Get("/api/logs", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(AppLogger::ReadLogs(), "text/plain; charset=utf-8");
    });

    svr->Post("/api/processes/watch", [this](const httplib::Request& req, httplib::Response& res) {
        if (!taskLogger_) {
            res.status = 500;
            res.set_content("{\"ok\":false,\"error\":\"TaskLogger not initialized\"}", "application/json");
            return;
        }

        if (!req.has_param("pid")) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"Missing pid\"}", "application/json");
            return;
        }

        try {
            auto pid = static_cast<uint32_t>(std::stoul(req.get_param_value("pid")));
            taskLogger_->addProcessPerPid(pid);
            res.set_content("{\"ok\":true}", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"Invalid pid\"}", "application/json");
        }
    });

    svr->listen(host.c_str(), port);
}

void Web::Stop() { svr->stop(); }
