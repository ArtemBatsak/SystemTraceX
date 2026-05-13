#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "../collectors/cpu/cpu.h"
#include "../collectors/disk/disk.h"
#include "../collectors/network/network.h"
#include "../collectors/ram/ram.h"
#include "../collectors/systeminfo/systemInfo.h"

namespace Telemetry {

struct Snapshot {
    uint64_t timestampMs = 0;
    CPU::CpuSnapshot cpu;
    Memory::MemorySnapshot memory;
    Disk::DiskSystemSnapshot disk;
    Net::NetworkSnapshot network;
    SystemInfo::SystemSnapshot system;
};

struct AggregatedSnapshot {
    uint64_t windowStartMs = 0;
    uint64_t windowEndMs = 0;

    double cpuAvg = 0.0;
    double cpuMin = 0.0;
    double cpuMax = 0.0;

    double ramUsedAvg = 0.0;
    double ramUsedMin = 0.0;
    double ramUsedMax = 0.0;

    double netRxAvg = 0.0;
    double netRxMin = 0.0;
    double netRxMax = 0.0;

    double netTxAvg = 0.0;
    double netTxMin = 0.0;
    double netTxMax = 0.0;
};

class TelemetryCollector {
public:
    static constexpr size_t kLiveCapacity = 600;      // 10 minutes @ 1 second
    static constexpr size_t kTenSecCapacity = 360;    // 1 hour @ 10 second aggregation
    static constexpr size_t kMinuteCapacity = 60;     // 1 hour @ 60 second aggregation

    explicit TelemetryCollector(std::string logDirectory);

    Snapshot CollectRawSnapshot();
    void PushLiveSnapshot(Snapshot snapshot);
    void FlushTenSecondAggregation();
    void FlushMinuteAggregation();

    Snapshot GetLastSnapshot() const;
    std::vector<Snapshot> GetLiveWindow() const;
    std::vector<AggregatedSnapshot> GetRecent24Hours() const;
    std::vector<AggregatedSnapshot> GetLongRange() const;

private:
    static uint64_t NowMs();
    static AggregatedSnapshot AggregateWindow(const std::vector<Snapshot>& window);
    void AppendBinary(const AggregatedSnapshot& snap, const std::string& fileName);

    std::string logDirectory_;

    mutable std::shared_mutex liveMutex_;
    std::deque<Snapshot> liveRing_;
    Snapshot lastSnapshot_;

    mutable std::mutex binaryMutex_;
    std::deque<AggregatedSnapshot> tenSecRing_;
    std::deque<AggregatedSnapshot> minuteRing_;
};

} // namespace Telemetry
