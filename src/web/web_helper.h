#pragma once

#include <cstdint>
#include <vector>

#include "../collector/collector.h"

namespace Web {

class WebTelemetryHelper {
public:
    explicit WebTelemetryHelper(Telemetry::TelemetryCollector& collector);

    Telemetry::Snapshot GetCurrentSnapshot();
    std::vector<Telemetry::Snapshot> GetLiveGraphBootstrap();
    std::vector<Telemetry::AggregatedSnapshot> Get24HoursSeries();
    std::vector<Telemetry::AggregatedSnapshot> GetLongRangeSeries();

private:
    Telemetry::TelemetryCollector& collector_;
};

} // namespace Web
