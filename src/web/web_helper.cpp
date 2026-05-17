#include "web_helper.h"
#include "web.h" // for json builders
#include <thread>
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

WebTelemetryHelper::WebTelemetryHelper(Telemetry::TelemetryCollector& collector)
    : collector_(collector) {}

WebTelemetryHelper::~WebTelemetryHelper()
{
    StopBackground();
}

void WebTelemetryHelper::StartBackground()
{
    bool expected = false;
    if (running_.exchange(true))
        return; // already running

    // Live thread updates frequently
    liveThread_ = std::thread([this]() {
        while (running_)
        {
            auto live = collector_.GetLiveWindow();
            auto snap = collector_.GetLastSnapshot();

            json liveJson = BuildLiveSeriesJson(live);
            json snapJson = json::object();
            snapJson["snapshot"] = json::object();

            // Build snapshot minimal - reuse existing BuildSnapshotJson logic moved into web.cpp
            // We'll build similar structure here inline to avoid header dependency cycles
            {
                const auto& cpu = snap.cpu;
                const auto& gpu = snap.gpu;
                const auto& ram = snap.memory;
                const auto& net = snap.network;
                const auto& system = snap.system;
                const auto& disk = snap.disk;

                json js;
                js["cpu"] = {{"usage", cpu.totalUsage}, {"name", cpu.cpuname}};
                js["ram"] = {{"total", ram.totalRAM}, {"used", ram.usedRAM}, {"free", ram.freeRAM}};
                js["gpus"] = json::array();
                for (const auto& g : gpu.gpus)
                {
                    js["gpus"].push_back({{"name", g.name}, {"usage", g.usagePercent}, {"vramTotal", g.vramTotalBytes}, {"vramUsed", g.vramUsedBytes}});
                }
                js["network"] = {{"rx", net.totalRxPerSec}, {"tx", net.totalTxPerSec}};
                js["network"]["interfaces"] = json::array();
                for (const auto& iface : net.interfaces)
                {
                    if (iface.ipv4.empty()) continue;
                    js["network"]["interfaces"].push_back({{"name", iface.name}, {"ipv4", iface.ipv4}, {"rx", iface.rxBytesPerSec}, {"tx", iface.txBytesPerSec}, {"rxTotal", iface.rxTotalBytes}, {"txTotal", iface.txTotalBytes}, {"isLoopback", iface.isLoopback}, {"isUp", iface.isUp}});
                }
                js["system"] = {{"hostname", system.hostname}, {"os", system.osName}, {"uptime", system.uptimeSeconds}, {"kernel", system.kernelVersion}, {"arch", system.architecture}, {"virtualization", {{"runningInVM", system.virtualization.runningInVM}, {"vendor", system.virtualization.vendor}}}};
                js["disks"] = json::array();
                for (const auto& d : disk.disks)
                {
                    js["disks"].push_back({{"name", d.name}, {"total", d.totalBytes}, {"free", d.freeBytes}, {"used", d.totalBytes - d.freeBytes}});
                }

                snapJson["snapshot"] = js;
            }

            {
                std::lock_guard<std::mutex> lk(cacheMutex_);
                liveCache_ = std::move(liveJson);
                snapshotCache_ = std::move(snapJson["snapshot"]);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    });

    // Aggregated thread updates less frequently
    aggThread_ = std::thread([this]() {
        while (running_)
        {
            auto hours = collector_.GetRecent24Hours();
            auto longRange = collector_.GetLongRange();
            auto sessions = GetSessionHistory();

            json hoursJson = BuildAggregatedSeriesJson(hours);
            json longJson = BuildAggregatedSeriesJson(longRange);
            json sessJson = BuildSessionHistoryJson(sessions);

            {
                std::lock_guard<std::mutex> lk(cacheMutex_);
                hours24Cache_ = std::move(hoursJson);
                longRangeCache_ = std::move(longJson);
                sessionsCache_ = std::move(sessJson);
            }

            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    });
}

void WebTelemetryHelper::StopBackground()
{
    if (!running_.exchange(false))
        return;

    if (liveThread_.joinable()) liveThread_.join();
    if (aggThread_.joinable()) aggThread_.join();
}

// JSON getters
nlohmann::json WebTelemetryHelper::GetSnapshotJson()
{
    std::lock_guard<std::mutex> lk(cacheMutex_);
    return snapshotCache_.is_null() ? json::object() : snapshotCache_;
}

nlohmann::json WebTelemetryHelper::GetLiveJson()
{
    std::lock_guard<std::mutex> lk(cacheMutex_);
    return liveCache_.is_null() ? json::array() : liveCache_;
}

nlohmann::json WebTelemetryHelper::Get24HoursJson()
{
    std::lock_guard<std::mutex> lk(cacheMutex_);
    return hours24Cache_.is_null() ? json::array() : hours24Cache_;
}

nlohmann::json WebTelemetryHelper::GetLongRangeJson()
{
    std::lock_guard<std::mutex> lk(cacheMutex_);
    return longRangeCache_.is_null() ? json::array() : longRangeCache_;
}

nlohmann::json WebTelemetryHelper::GetSessionHistoryJson()
{
    std::lock_guard<std::mutex> lk(cacheMutex_);
    return sessionsCache_.is_null() ? json::array() : sessionsCache_;
}

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


