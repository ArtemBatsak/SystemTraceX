#pragma once

#include <cstdint>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <string> 

#include "../collector/collector.h"

class WebTelemetryHelper {
public:
    explicit WebTelemetryHelper(Telemetry::TelemetryCollector& collector);

    std::string GetLiveWindowString();
    std::string GetAggregatedWindowString(std::string type);
    std::string GetSnapshotString();
    std::string GetProcessesString(int count);
    std::string GetErrorsString();

private:
    Telemetry::TelemetryCollector& collector_;
};

static std::string EscapeJsonString(const std::string& input) {
    std::string output;
    output.reserve(input.length()); // Минимизируем реалокации
    for (char c : input) {
        switch (c) {
        case '\\': output += "\\\\"; break;
        case '"':  output += "\\\""; break;
        case '\b': output += "\\b";  break;
        case '\f': output += "\\f";  break;
        case '\n': output += "\\n";  break;
        case '\r': output += "\\r";  break;
        case '\t': output += "\\t";  break;
        default:   output += c;      break;
        }
    }
    return output;
}
