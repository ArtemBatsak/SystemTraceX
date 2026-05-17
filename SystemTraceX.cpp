#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>

#include "src/web/web_helper.h"
#include "src/collector/collector.h"
#include "src/web/web.h"

int main() {

    Telemetry::TelemetryCollector collector("./telemetry_logs");
    WebTelemetryHelper webHelper(collector);

    std::atomic<bool> running{ true };
    std::thread telemetryThread([&]() {
        int ticks = 0;
        while (running.load()) {
            collector.PushLiveSnapshot(collector.CollectRawSnapshot());
            ++ticks;
            if (ticks % 10 == 0) collector.FlushTenSecondAggregation();
            if (ticks % 60 == 0) collector.FlushMinuteAggregation();
			std::cout << "Collected snapshot at " << ticks << " seconds" << std::endl;
			
            if (ticks <= 120) { 
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
			}
            else {
                std::cout << "seconds:" << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
			}

        }
    });
    Web web(webHelper);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::thread webThread([&]()
        {
            web.Start("0.0.0.0", 8080);
        });

	telemetryThread.join();
	webThread.join();

    return 0;
}
