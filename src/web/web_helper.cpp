#include "web_helper.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <vector>
#include "../collectors/process/process.h"
#include "../collectors/errors/errors.h"

WebTelemetryHelper::WebTelemetryHelper(Telemetry::TelemetryCollector& collector)
    : collector_(collector) {
}

std::string WebTelemetryHelper::GetSnapshotString()
{
	auto snap = collector_.GetLastSnapshot();
    const auto& cpu = snap.cpu;
    const auto& gpu = snap.gpu;
    const auto& ram = snap.memory;
    const auto& net = snap.network;
    const auto& system = snap.system;
    const auto& disk = snap.disk;

    std::ostringstream ss;
    
    ss << std::fixed << std::setprecision(6);

    ss << "{\n";

    // 1. CPU
    ss << "  \"cpu\": {\n"
        << "    \"name\": \"" << EscapeJsonString(cpu.cpuname) << "\",\n"
        << "    \"usage\": " << cpu.totalUsage << "\n"
        << "  },\n";

    // 2. DISKS
    ss << "  \"disks\": [\n";
    for (size_t i = 0; i < disk.disks.size(); ++i) {
        const auto& d = disk.disks[i];
        ss << "    {\n"
            << "      \"free\": " << d.freeBytes << ",\n"
            << "      \"name\": \"" << EscapeJsonString(d.name) << "\",\n" // Тут важно, чтобы пути вроде C:\ были уже экранированы (C:\\), либо используй ручную замену
            << "      \"total\": " << d.totalBytes << ",\n"
            << "      \"used\": " << (d.totalBytes - d.freeBytes) << "\n"
            << "    }" << (i + 1 < disk.disks.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // 3. GPUS
    ss << "  \"gpus\": [\n";
    for (size_t i = 0; i < gpu.gpus.size(); ++i) {
        const auto& g = gpu.gpus[i];
        ss << "    {\n"
            << "      \"name\": \"" << EscapeJsonString(g.name) << "\",\n"
            << "      \"usage\": " << g.usagePercent << ",\n"
            << "      \"vramTotal\": " << g.vramTotalBytes << ",\n"
            << "      \"vramUsed\": " << g.vramUsedBytes << "\n"
            << "    }" << (i + 1 < gpu.gpus.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // 4. NETWORK
    ss << "  \"network\": {\n"
        << "    \"interfaces\": [\n";

    // Сначала фильтруем интерфейсы, чтобы правильно расставить запятые
    std::vector<size_t> valid_ifaces;
    for (size_t i = 0; i < net.interfaces.size(); ++i) {
        if (!net.interfaces[i].ipv4.empty()) {
            valid_ifaces.push_back(i);
        }
    }

    for (size_t i = 0; i < valid_ifaces.size(); ++i) {
        const auto& iface = net.interfaces[valid_ifaces[i]];
        ss << "      {\n"
            << "        \"ipv4\": \"" << iface.ipv4 << "\",\n"
            << "        \"isLoopback\": " << (iface.isLoopback ? "true" : "false") << ",\n"
            << "        \"isUp\": " << (iface.isUp ? "true" : "false") << ",\n"
            << "        \"name\": \"" << EscapeJsonString(iface.name) << "\",\n"
            << "        \"rx\": " << iface.rxBytesPerSec << ",\n"
            << "        \"rxTotal\": " << iface.rxTotalBytes << ",\n"
            << "        \"tx\": " << iface.txBytesPerSec << ",\n"
            << "        \"txTotal\": " << iface.txTotalBytes << "\n"
            << "      }" << (i + 1 < valid_ifaces.size() ? "," : "") << "\n";
    }
    ss << "    ],\n"
        << "    \"rx\": " << net.totalRxPerSec << ",\n"
        << "    \"tx\": " << net.totalTxPerSec << "\n"
        << "  },\n";

    // 5. RAM
    ss << "  \"ram\": {\n"
        << "    \"free\": " << ram.freeRAM << ",\n"
        << "    \"total\": " << ram.totalRAM << ",\n"
        << "    \"used\": " << ram.usedRAM << "\n"
        << "  },\n";

    // 6. SYSTEM
    ss << "  \"system\": {\n"
        << "    \"arch\": \"" << system.architecture << "\",\n"
        << "    \"hostname\": \"" << EscapeJsonString(system.hostname) << "\",\n"
        << "    \"kernel\": \"" << EscapeJsonString(system.kernelVersion) << "\",\n"
        << "    \"os\": \"" << EscapeJsonString(system.osName) << "\",\n"
        << "    \"uptime\": " << system.uptimeSeconds << ",\n"
        << "    \"virtualization\": {\n"
        << "      \"runningInVM\": " << (system.virtualization.runningInVM ? "true" : "false") << ",\n"
        << "      \"vendor\": \"" << EscapeJsonString(system.virtualization.vendor) << "\"\n"
        << "    }\n"
        << "  },\n";

    // 7. TIME
    ss << "  \"time\": " << snap.timestampMs << "\n";

    ss << "}";

    return ss.str();
}

std::string WebTelemetryHelper::GetLiveWindowString()
{
	auto window = collector_.GetLiveWindow();
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);

    ss << "[\n";

    for (size_t i = 0; i < window.size(); ++i)
    {
        const auto& snap = window[i];
        const auto& cpu = snap.cpu;
        const auto& gpu = snap.gpu;
        const auto& ram = snap.memory;
        const auto& net = snap.network;
        const auto& disk = snap.disk;

        // Считаем общий процент по RAM
        double ramPercent = 0.0;
        if (ram.totalRAM > 0) {
            ramPercent = (static_cast<double>(ram.usedRAM) / ram.totalRAM) * 100.0;
        }

        // Считаем общую емкость и занятое место по всем дискам для общего "disk": {"percent": ...}
        unsigned long long totalDisksSize = 0;
        unsigned long long totalDisksUsed = 0;

        ss << "  {\n"
            << "    \"cpu\": " << cpu.totalUsage << ",\n";

        // Собираем массив дисков внутри объекта
        ss << "    \"disks\": [\n";
        for (size_t j = 0; j < disk.disks.size(); ++j)
        {
            const auto& d = disk.disks[j];
            unsigned long long usedBytes = d.totalBytes - d.freeBytes;

            totalDisksSize += d.totalBytes;
            totalDisksUsed += usedBytes;

            double diskPercent = 0.0;
            if (d.totalBytes > 0) {
                diskPercent = (static_cast<double>(usedBytes) / d.totalBytes) * 100.0;
            }

            ss << "      {\n"
                << "        \"free\": " << d.freeBytes << ",\n"
                << "        \"name\": \"" << EscapeJsonString(d.name) << "\",\n"
                << "        \"percent\": " << diskPercent << ",\n"
                << "        \"total\": " << d.totalBytes << ",\n"
                << "        \"used\": " << usedBytes << "\n"
                << "      }" << (j + 1 < disk.disks.size() ? "," : "") << "\n";
        }
        ss << "    ],\n";

        // Считаем общий процент дисков
        double totalDiskPercent = 0.0;
        if (totalDisksSize > 0) {
            totalDiskPercent = (static_cast<double>(totalDisksUsed) / totalDisksSize) * 100.0;
        }

        ss << "    \"disk\": {\n"
            << "      \"percent\": " << totalDiskPercent << "\n"
            << "    },\n";

        // Берем загрузку первой GPU (или 0.0, если видеокарт нет)
        double gpuUsage = 0.0;
        if (!gpu.gpus.empty()) {
            gpuUsage = gpu.gpus[0].usagePercent; // Если нужно суммировать или выводить массив — скажи, переделаем
        }
        ss << "    \"gpu\": " << gpuUsage << ",\n";

        // Сеть
        ss << "    \"network\": {\n"
            << "      \"rx\": " << net.totalRxPerSec << ",\n"
            << "      \"tx\": " << net.totalTxPerSec << "\n"
            << "    },\n";

        // RAM
        ss << "    \"ram\": {\n"
            << "      \"percent\": " << ramPercent << ",\n"
            << "      \"total\": " << ram.totalRAM << ",\n"
            << "      \"used\": " << ram.usedRAM << "\n"
            << "    },\n";

        // Время (t)
        ss << "    \"t\": " << snap.timestampMs << "\n";

        // Закрываем текущий объект снапшота в массиве
        ss << "  }" << (i + 1 < window.size() ? "," : "") << "\n";
    }

    ss << "]";
    return ss.str();
}

std::string WebTelemetryHelper::GetAggregatedWindowString(std::string type)
{
	auto aggWindow = std::vector<Telemetry::AggregatedSnapshot>{};
    if (type == "24h") {
        aggWindow = collector_.GetRecent24Hours();
        
    }
    else if (type == "long") {
        aggWindow = collector_.GetLongRange();
        
    }
    else {
        return "[]"; // Пустой массив для неизвестного типа
	}
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);

    ss << "[\n";

    for (size_t i = 0; i < aggWindow.size(); ++i)
    {
        const auto& snap = aggWindow[i];

        uint8_t recordType = static_cast<uint8_t>(snap.recordType);
        std::string typeStr;
        switch (recordType) {
        case 0:      typeStr = "Metric"; break;
        case 1:      typeStr = "SessionStart"; break;
        case 2:      typeStr = "SessionEnd"; break;
        default:     typeStr = "Unknown"; break;
        }

        ss << "  {\n"
            << "    \"type\": \"" << typeStr << "\",\n"
            << "    \"windowStartMs\": " << snap.windowStartMs << ",\n"
            << "    \"windowEndMs\": " << snap.windowEndMs << ",\n"
            << "    \"sessionDurationMs\": " << snap.sessionDurationMs << ",\n";

        // 1. CPU (Min, Max, Avg)
        ss << "    \"cpu\": {\n"
            << "      \"avg\": " << snap.cpuAvg << ",\n"
            << "      \"min\": " << snap.cpuMin << ",\n"
            << "      \"max\": " << snap.cpuMax << "\n"
            << "    },\n";

        // 2. GPU (Min, Max, Avg)
        ss << "    \"gpu\": {\n"
            << "      \"avg\": " << snap.gpuAvg << ",\n"
            << "      \"min\": " << snap.gpuMin << ",\n"
            << "      \"max\": " << snap.gpuMax << "\n"
            << "    },\n";

        // 3. RAM (Только usedAvg, usedMin, usedMax)
        ss << "    \"ram\": {\n"
            << "      \"usedAvg\": " << snap.ramUsedAvg << ",\n"
            << "      \"usedMin\": " << snap.ramUsedMin << ",\n"
            << "      \"usedMax\": " << snap.ramUsedMax << "\n"
            << "    }\n"; // Последний элемент в объекте, запятая в конце не нужна

        // Закрываем объект элемента массива
        ss << "  }" << (i + 1 < aggWindow.size() ? "," : "") << "\n";
    }

    ss << "]";
    return ss.str();
}

std::string WebTelemetryHelper::GetProcessesString()
{
    auto processSnapshot = Proc::GetSnapshot(40);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "{\n";
    ss << "  \"totalProcesses\": " << processSnapshot.totalProcesses << ",\n";
    ss << "  \"topProcesses\": [\n";

    const size_t limit = (std::min)(static_cast<size_t>(40), processSnapshot.topProcesses.size());
    for (size_t i = 0; i < limit; ++i) {
        const auto& p = processSnapshot.topProcesses[i];
        ss << "    {\n"
            << "      \"pid\": " << p.pid << ",\n"
            << "      \"name\": \"" << EscapeJsonString(p.name) << "\",\n"
            << "      \"cpuUsage\": " << p.cpuUsage << ",\n"
            << "      \"memoryUsage\": " << p.memoryUsage << ",\n"
            << "      \"importanceScore\": " << p.importanceScore << "\n"
            << "    }" << (i + 1 < limit ? "," : "") << "\n";
    }

    ss << "  ]\n";
    ss << "}";
    return ss.str();
}
 
std::string WebTelemetryHelper::GetErrorsString() {

    auto errorSnapshot = SystemErrors::GetSnapshot();

    auto severityToString = [](SystemErrors::Severity severity) -> std::string_view {
        switch (severity) {
        case SystemErrors::Severity::Critical: return "Critical";
        case SystemErrors::Severity::Error:    return "Error";
        case SystemErrors::Severity::Warning:  return "Warning";
        case SystemErrors::Severity::Info:     return "Info";
        default:                               return "Unknown";
        }
        };

    auto appendEvent = [&](std::string& out, const auto& events) {
        for (size_t i = 0; i < events.size(); ++i) {
            const auto& e = events[i];
            out.append("    {\n      \"timestamp\": ").append(std::to_string(e.timestamp))
                .append(",\n      \"source\": \"").append(EscapeJsonString(e.source))
                .append("\",\n      \"severity\": \"").append(severityToString(e.severity))
                .append("\",\n      \"message\": \"").append(EscapeJsonString(e.message))
                .append("\",\n      \"eventId\": ").append(std::to_string(e.eventId))
                .append("\n    }");
            if (i + 1 < events.size()) out.append(",");
            out.append("\n");
        }
        };

    std::string result;
    // Резервируем память, чтобы избежать частых реаллокаций
    result.reserve(2048 + (errorSnapshot.lastEvents.size() + errorSnapshot.criticalEvents.size()) * 256);

    result.append("{\n  \"lastEvents\": [\n");
    appendEvent(result, errorSnapshot.lastEvents);
    result.append("  ],\n  \"criticalEvents\": [\n");
    appendEvent(result, errorSnapshot.criticalEvents);
    result.append("  ]\n}");

    return result;
}
