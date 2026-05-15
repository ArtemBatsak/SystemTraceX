#include <atomic>
#include <chrono>
#include <thread>

#include "src/collector/collector.h"
#include "src/web/web.h"
#include "src/web/web_helper.h"

int main() {
    Telemetry::TelemetryCollector collector("./telemetry_logs");
    WebTelemetryHelper webHelper(collector);
    Web web(webHelper);

    std::atomic<bool> running{true};
    std::thread telemetryThread([&]() {
        int ticks = 0;
        while (running.load()) {
            collector.PushLiveSnapshot(collector.CollectRawSnapshot());
            ++ticks;
            if (ticks % 10 == 0) collector.FlushTenSecondAggregation();
            if (ticks % 60 == 0) collector.FlushMinuteAggregation();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
    auto webThread = std::thread([&]() {
        web.Start(8080);
	});
   

	webThread.join();
    telemetryThread.join();
    return 0;
}
