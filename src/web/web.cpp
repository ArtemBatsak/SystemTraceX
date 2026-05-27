#include "web.h"


Web::Web(WebTelemetryHelper& webHelper_)
    : webHelper(webHelper_) {
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
        res.set_content(webHelper.GetProcessesString(10), "application/json");
    });
    svr->Get("/api/errors", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(webHelper.GetErrorsString(), "application/json");
    });
    

    svr->listen(host.c_str(), port);
}

void Web::Stop() { svr->stop(); }
