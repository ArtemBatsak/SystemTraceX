#include "web.h"

#include <sstream>
#include <fstream>

Web::Web(WebTelemetryHelper& webHelper_)
    : webHelper(webHelper_)
{
    svr = std::make_unique<httplib::Server>();
    webHelper.StartBackground();
}

void Web::Start(const std::string& host, int port)
{
    // ---------------- HTML ----------------

    svr->Get("/", [this](const httplib::Request&, httplib::Response& res)
        {
            res.set_content(ReadFile("web/index.html"), "text/html; charset=utf-8");
        });
    svr->Get("/style.css", [this](const httplib::Request&, httplib::Response& res)
        {
            res.set_content(ReadFile("web/style.css"), "text/css; charset=utf-8");
        });
    svr->Get("/app.js", [this](const httplib::Request&, httplib::Response& res)
        {
            res.set_content(ReadFile("web/app.js"), "application/javascript; charset=utf-8");
        });

    // ---------------- API ----------------

    svr->Get("/api/snapshot",
        [this](const httplib::Request&, httplib::Response& res)
        {
            res.set_content(webHelper.GetSnapshotJson().dump(), "application/json");
        });
    svr->Get("/api/telemetry/24h",
        [this](const httplib::Request&, httplib::Response& res)
        {
            res.set_content(webHelper.Get24HoursJson().dump(), "application/json");
        });

    svr->Get("/api/telemetry/long",
        [this](const httplib::Request&, httplib::Response& res)
        {
            res.set_content(webHelper.GetLongRangeJson().dump(), "application/json");
        });
    svr->Get("/api/telemetry/sessions",
        [this](const httplib::Request&, httplib::Response& res)
        {
            res.set_content(webHelper.GetSessionHistoryJson().dump(), "application/json");
        });
    svr->Get("/api/telemetry/live",
        [this](const httplib::Request&, httplib::Response& res)
        {
            res.set_content(webHelper.GetLiveJson().dump(), "application/json");
        });

    svr->listen(host.c_str(), port);
}

void Web::Stop()
{
    webHelper.StopBackground();
    svr->stop();
}



json BuildAggregatedSeriesJson(
    const std::vector<Telemetry::AggregatedSnapshot>& data)
{
    json j = json::array();

    for (const auto& s : data)
    {
        j.push_back({

            // ---------------- TIME ----------------

            {"start", s.windowStartMs},
            {"end", s.windowEndMs},
            {"duration", s.sessionDurationMs},

            // ---------------- CPU ----------------

            {"cpu", {
                {"avg", s.cpuAvg},
                {"min", s.cpuMin},
                {"max", s.cpuMax}
            }},

            // ---------------- GPU ----------------

            {"gpu", {
                {"avg", s.gpuAvg},
                {"min", s.gpuMin},
                {"max", s.gpuMax}
            }},

            // ---------------- RAM ----------------

            {"ram", {
                {"percentAvg", s.ramPercentAvg},

                {"usedAvg", s.ramUsedAvg},
                {"usedMin", s.ramUsedMin},
                {"usedMax", s.ramUsedMax}
            }},

            // ---------------- DISK ----------------

            {"disk", {
                {"percentAvg", s.diskPercentAvg}
            }},

            // ---------------- NETWORK ----------------

            {"network", {

                {"rx", {
                    {"avg", s.netRxAvg},
                    {"min", s.netRxMin},
                    {"max", s.netRxMax}
                }},

                {"tx", {
                    {"avg", s.netTxAvg},
                    {"min", s.netTxMin},
                    {"max", s.netTxMax}
                }}

            }}
            });
    }

    return j;
}
json BuildLiveSeriesJson(
    const std::vector<Telemetry::Snapshot>& data)
{
    json j = json::array();

    for (const auto& s : data)
    {
        // ---------------- GPU ----------------

        double gpuUsage = 0.0;

        if (!s.gpu.gpus.empty())
        {
            for (const auto& g : s.gpu.gpus)
                gpuUsage += g.usagePercent;

            gpuUsage /= static_cast<double>(s.gpu.gpus.size());
        }

        // ---------------- DISK TOTAL ----------------

        uint64_t diskTotal = 0;
        uint64_t diskFree = 0;

        for (const auto& d : s.disk.disks)
        {
            diskTotal += d.totalBytes;
            diskFree += d.freeBytes;
        }

        double diskPercent = 0.0;

        if (diskTotal > 0)
        {
            diskPercent =
                100.0 *
                static_cast<double>(diskTotal - diskFree) /
                static_cast<double>(diskTotal);
        }

        // ---------------- ROOT OBJECT ----------------

        json entry = {

            // ---------------- TIME ----------------

            {"t", s.timestampMs},

            // ---------------- CPU ----------------

            {"cpu", s.cpu.totalUsage},

            // ---------------- GPU ----------------

            {"gpu", gpuUsage},

            // ---------------- RAM ----------------

            {"ram", {
                {"used", s.memory.usedRAM},
                {"total", s.memory.totalRAM},
                {"percent",
                    s.memory.totalRAM > 0
                    ? 100.0 *
                      static_cast<double>(s.memory.usedRAM) /
                      static_cast<double>(s.memory.totalRAM)
                    : 0.0
                }
            }},

            // ---------------- DISK TOTAL ----------------

            {"disk", {
                {"percent", diskPercent}
            }},

            // ---------------- NETWORK ----------------

            {"network", {
                {"rx", s.network.totalRxPerSec},
                {"tx", s.network.totalTxPerSec}
            }}
        };

        // ---------------- PER DISK ----------------

        entry["disks"] = json::array();

        for (const auto& d : s.disk.disks)
        {
            double percent = 0.0;

            if (d.totalBytes > 0)
            {
                percent =
                    100.0 *
                    static_cast<double>(d.totalBytes - d.freeBytes) /
                    static_cast<double>(d.totalBytes);
            }

            entry["disks"].push_back({

                {"name", d.name},

                {"total", d.totalBytes},
                {"free", d.freeBytes},
                {"used", d.totalBytes - d.freeBytes},

                {"percent", percent}
                });
        }

        j.push_back(entry);
    }

    return j;
}
json BuildSessionHistoryJson(
    const std::vector<std::vector<Telemetry::AggregatedSnapshot>>& sessions)
{
    json out = json::array();

    for (const auto& session : sessions)
    {
        if (session.empty())
            continue;

        json metrics = json::array();

        uint64_t start = 0;
        uint64_t end = 0;
        uint64_t duration = 0;

        for (const auto& s : session)
        {
            // SESSION START
            if (s.recordType ==
                Telemetry::AggregatedRecordType::SessionStart)
            {
                start = s.windowStartMs;
                continue;
            }

            // SESSION END
            if (s.recordType ==
                Telemetry::AggregatedRecordType::SessionEnd)
            {
                end = s.windowEndMs;
                duration = s.sessionDurationMs;
                continue;
            }

            // REAL METRICS
            metrics.push_back({

                {"start", s.windowStartMs},
                {"end", s.windowEndMs},

                {"cpu", {
                    {"avg", s.cpuAvg},
                    {"min", s.cpuMin},
                    {"max", s.cpuMax}
                }},

                {"gpu", {
                    {"avg", s.gpuAvg},
                    {"min", s.gpuMin},
                    {"max", s.gpuMax}
                }},

                {"ram", {
                    {"percentAvg", s.ramPercentAvg}
                }},

                {"network", {
                    {"rx", s.netRxAvg},
                    {"tx", s.netTxAvg}
                }}
                });
        }

        out.push_back({

            {"start", start},
            {"end", end},
            {"duration", duration},

            {"data", metrics}
            });
    }

    return out;
}
