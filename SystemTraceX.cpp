#include <chrono>
#include <iostream>
#include <thread>

#include "src/collector/collector.h"
#include "src/web/web_helper.h"

int main() {
    Telemetry::TelemetryCollector collector("./telemetry_logs");
    Web::WebTelemetryHelper webHelper(collector);

    for (int second = 1; second <= 75; ++second) {
        auto snapshot = collector.CollectRawSnapshot();
        collector.PushLiveSnapshot(snapshot);

        if (second % 10 == 0) {
            collector.FlushTenSecondAggregation();
        }
        if (second % 60 == 0) {
            collector.FlushMinuteAggregation();
        }

        if (second % 5 == 0) {
            auto current = webHelper.GetCurrentSnapshot();
            auto live = webHelper.GetLiveGraphBootstrap();
            auto shortSeries = webHelper.Get24HoursSeries();
            auto longSeries = webHelper.GetLongRangeSeries();

            std::cout << "--- Web Helper Probe (t=" << second << "s) ---\n";
            std::cout << "CPU: " << current.cpu.totalUsage << "%\n";
            std::cout << "RAM used: " << current.memory.usedRAM / (1024.0 * 1024.0) << " MB\n";
            std::cout << "Disks: " << current.disk.disks.size() << "\n";
            std::cout << "Network interfaces: " << current.network.interfaces.size() << "\n";
            std::cout << "System: " << current.system.osName << " / " << current.system.architecture << "\n";
            std::cout << "Live ring size: " << live.size() << "\n";
            std::cout << "10s aggregated ring size: " << shortSeries.size() << "\n";
            std::cout << "60s aggregated ring size: " << longSeries.size() << "\n\n";
            std::cout << current.timestampMs << " ms since epoch\n";
            auto disks = current.disk.disks;
            for (const auto& disk : disks) {
                std::cout << "  Disk " << disk.name << ": " << disk.freeBytes / (1024.0 * 1024.0 * 1024.0) << " GB free / "
                    << disk.totalBytes / (1024.0 * 1024.0 * 1024.0) << " GB total\n";
            }
            std::cout << "GPU(s): " << current.gpu.gpus.size() << "\n";
			auto gpus = current.gpu.gpus;
            for (const auto& gpu : gpus) {
                std::cout << "  GPU " << gpu.name << ": " << gpu.vramUsedBytes / (1024.0 * 1024.0) << " MB used / "
                    << gpu.vramTotalBytes / (1024.0 * 1024.0) << " MB total\n";
                std::cout << "    Vendor: " << (gpu.vendor == GPU::Vendor::Nvidia ? "Nvidia" :
                    gpu.vendor == GPU::Vendor::AMD ? "AMD" :
					gpu.vendor == GPU::Vendor::Intel ? "Intel" : "Unknown") << "\n";
				std::cout << "    Type: " << (gpu.isIntegrated ? "Integrated" : "Discrete") << "\n";
				std::cout << "    Usage: " << gpu.usagePercent << "%\n";
            }


        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}