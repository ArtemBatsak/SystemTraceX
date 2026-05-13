#include "collector.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

namespace Telemetry {

TelemetryCollector::TelemetryCollector(std::string logDirectory)
    : logDirectory_(std::move(logDirectory)) {
    std::filesystem::create_directories(logDirectory_);
}

Snapshot TelemetryCollector::CollectRawSnapshot() {
    Snapshot snapshot;
    snapshot.timestampMs = NowMs();
    CPU::Update();
    snapshot.cpu = CPU::GetSnapshot();
    snapshot.memory = Memory::GetSnapshot();
    snapshot.disk = Disk::GetSnapshot();
    snapshot.network = Net::GetSnapshot();
    snapshot.system = SystemInfo::GetSnapshot();
    return snapshot;
}

void TelemetryCollector::PushLiveSnapshot(Snapshot snapshot) {
    std::unique_lock<std::shared_mutex> lock(liveMutex_);
    lastSnapshot_ = snapshot;
    liveRing_.push_back(std::move(snapshot));
    if (liveRing_.size() > kLiveCapacity) {
        liveRing_.pop_front();
    }
}

void TelemetryCollector::FlushTenSecondAggregation() {
    std::vector<Snapshot> window;
    {
        std::shared_lock<std::shared_mutex> lock(liveMutex_);
        if (liveRing_.size() < 10) return;
        window.assign(liveRing_.end() - 10, liveRing_.end());
    }

    auto aggregated = AggregateWindow(window);

    std::lock_guard<std::mutex> lock(binaryMutex_);
    tenSecRing_.push_back(aggregated);
    if (tenSecRing_.size() > kTenSecCapacity) {
        tenSecRing_.pop_front();
    }
    AppendBinary(aggregated, "telemetry_10s.bin");
}

void TelemetryCollector::FlushMinuteAggregation() {
    std::deque<AggregatedSnapshot> source;
    {
        std::lock_guard<std::mutex> lock(binaryMutex_);
        if (tenSecRing_.size() < 6) return;
        source = tenSecRing_;
    }

    AggregatedSnapshot m;
    m.windowStartMs = source[source.size() - 6].windowStartMs;
    m.windowEndMs = source.back().windowEndMs;

    auto minMaxAvg = [](const std::array<double, 6>& vals) {
        double minV = std::numeric_limits<double>::max();
        double maxV = std::numeric_limits<double>::lowest();
        double sum = 0.0;
        for (double v : vals) {
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
            sum += v;
        }
        return std::array<double, 3>{sum / vals.size(), minV, maxV};
    };

    std::array<double, 6> cpuVals{}, ramVals{}, rxVals{}, txVals{};
    for (size_t i = 0; i < 6; ++i) {
        const auto& s = source[source.size() - 6 + i];
        cpuVals[i] = s.cpuAvg;
        ramVals[i] = s.ramUsedAvg;
        rxVals[i] = s.netRxAvg;
        txVals[i] = s.netTxAvg;
    }

    auto cpu = minMaxAvg(cpuVals); m.cpuAvg = cpu[0]; m.cpuMin = cpu[1]; m.cpuMax = cpu[2];
    auto ram = minMaxAvg(ramVals); m.ramUsedAvg = ram[0]; m.ramUsedMin = ram[1]; m.ramUsedMax = ram[2];
    auto rx = minMaxAvg(rxVals); m.netRxAvg = rx[0]; m.netRxMin = rx[1]; m.netRxMax = rx[2];
    auto tx = minMaxAvg(txVals); m.netTxAvg = tx[0]; m.netTxMin = tx[1]; m.netTxMax = tx[2];

    std::lock_guard<std::mutex> lock(binaryMutex_);
    minuteRing_.push_back(m);
    if (minuteRing_.size() > kMinuteCapacity) {
        minuteRing_.pop_front();
    }
    AppendBinary(m, "telemetry_60s.bin");
}

Snapshot TelemetryCollector::GetLastSnapshot() const {
    std::shared_lock<std::shared_mutex> lock(liveMutex_);
    return lastSnapshot_;
}

std::vector<Snapshot> TelemetryCollector::GetLiveWindow() const {
    std::shared_lock<std::shared_mutex> lock(liveMutex_);
    return {liveRing_.begin(), liveRing_.end()};
}

std::vector<AggregatedSnapshot> TelemetryCollector::GetRecent24Hours() const {
    std::lock_guard<std::mutex> lock(binaryMutex_);
    return {tenSecRing_.begin(), tenSecRing_.end()};
}

std::vector<AggregatedSnapshot> TelemetryCollector::GetLongRange() const {
    std::lock_guard<std::mutex> lock(binaryMutex_);
    return {minuteRing_.begin(), minuteRing_.end()};
}

uint64_t TelemetryCollector::NowMs() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
    return static_cast<uint64_t>(now.time_since_epoch().count());
}

AggregatedSnapshot TelemetryCollector::AggregateWindow(const std::vector<Snapshot>& window) {
    AggregatedSnapshot out;
    if (window.empty()) return out;

    out.windowStartMs = window.front().timestampMs;
    out.windowEndMs = window.back().timestampMs;

    double cpuMin = std::numeric_limits<double>::max();
    double cpuMax = std::numeric_limits<double>::lowest();
    double cpuSum = 0.0;

    double ramMin = std::numeric_limits<double>::max();
    double ramMax = std::numeric_limits<double>::lowest();
    double ramSum = 0.0;

    double rxMin = std::numeric_limits<double>::max();
    double rxMax = std::numeric_limits<double>::lowest();
    double rxSum = 0.0;

    double txMin = std::numeric_limits<double>::max();
    double txMax = std::numeric_limits<double>::lowest();
    double txSum = 0.0;

    for (const auto& s : window) {
        const double cpu = s.cpu.totalUsage;
        const double ram = static_cast<double>(s.memory.usedRAM);
        const double rx = s.network.totalRxPerSec;
        const double tx = s.network.totalTxPerSec;

        cpuSum += cpu; cpuMin = std::min(cpuMin, cpu); cpuMax = std::max(cpuMax, cpu);
        ramSum += ram; ramMin = std::min(ramMin, ram); ramMax = std::max(ramMax, ram);
        rxSum += rx; rxMin = std::min(rxMin, rx); rxMax = std::max(rxMax, rx);
        txSum += tx; txMin = std::min(txMin, tx); txMax = std::max(txMax, tx);
    }

    const double n = static_cast<double>(window.size());
    out.cpuAvg = cpuSum / n; out.cpuMin = cpuMin; out.cpuMax = cpuMax;
    out.ramUsedAvg = ramSum / n; out.ramUsedMin = ramMin; out.ramUsedMax = ramMax;
    out.netRxAvg = rxSum / n; out.netRxMin = rxMin; out.netRxMax = rxMax;
    out.netTxAvg = txSum / n; out.netTxMin = txMin; out.netTxMax = txMax;
    return out;
}

void TelemetryCollector::AppendBinary(const AggregatedSnapshot& snap, const std::string& fileName) {
    std::ofstream out(logDirectory_ + "/" + fileName, std::ios::binary | std::ios::app);
    if (!out.is_open()) return;
    out.write(reinterpret_cast<const char*>(&snap), sizeof(AggregatedSnapshot));
}

} // namespace Telemetry
