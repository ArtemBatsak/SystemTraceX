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
#include "../collectors/gpu/gpu.h"
#include "../collectors/disk/disk.h"
#include "../collectors/network/network.h"
#include "../collectors/ram/ram.h"
#include "../collectors/systeminfo/systemInfo.h"

namespace Telemetry {

enum class AggregatedRecordType : uint8_t {
    Metric = 0,
    SessionStart = 1,
    SessionEnd = 2,
};

struct Snapshot {
    uint64_t timestampMs = 0;
    CPU::CpuSnapshot cpu;
    Memory::MemorySnapshot memory;
    Disk::DiskSystemSnapshot disk;
    Net::NetworkSnapshot network;
    SystemInfo::SystemSnapshot system;
	GPU::GpuSnapshot gpu;
};

struct AggregatedSnapshot {
    AggregatedRecordType recordType = AggregatedRecordType::Metric;
    uint64_t windowStartMs = 0;
    uint64_t windowEndMs = 0;
    uint64_t sessionDurationMs = 0;

    double cpuAvg = 0.0;
    double cpuMin = 0.0;
    double cpuMax = 0.0;

	double gpyAvg = 0.0;
	double gpuMin = 0.0;
	double gpuMax = 0.0;

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
    static constexpr size_t kTenSecCapacity = 8640;   // 24 hours @ 10 second aggregation
    static constexpr uintmax_t kLongFileStartupMaxBytes = 1u * 1024u * 1024u;
    static constexpr uintmax_t kLongFileRuntimeRotateBytes = 2u * 1024u * 1024u;

    explicit TelemetryCollector(std::string logDirectory);
    ~TelemetryCollector();

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

    void AppendLongRecord(const AggregatedSnapshot& snap);
    std::vector<AggregatedSnapshot> ReadLongRecords(const std::string& filePath) const;
    std::vector<AggregatedSnapshot> ReadCombinedLongRecords() const;
    bool RotateLongFileIfNeeded(uintmax_t maxBytes);
    std::string RotatedLongFilePath() const;
    void WriteSessionStart(uint64_t tsMs);
    void WriteSessionEnd(uint64_t startTsMs, uint64_t endTsMs);
    void RecoverUnclosedSession();

    std::string logDirectory_;
    std::string longFilePath_;

    mutable std::shared_mutex liveMutex_;
    std::deque<Snapshot> liveRing_;
    Snapshot lastSnapshot_;

    mutable std::mutex binaryMutex_;
    std::deque<AggregatedSnapshot> tenSecRing_;
    uint64_t currentSessionStartMs_ = 0;
    bool sessionOpen_ = false;
};

} // namespace Telemetry
