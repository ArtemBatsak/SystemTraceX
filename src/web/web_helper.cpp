#include "web_helper.h"

namespace Web {

WebTelemetryHelper::WebTelemetryHelper(Telemetry::TelemetryCollector& collector)
    : collector_(collector) {}

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

} // namespace Web
