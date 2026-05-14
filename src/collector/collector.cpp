#include "collector.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

namespace Telemetry {

TelemetryCollector::TelemetryCollector(std::string logDirectory)
    : logDirectory_(std::move(logDirectory)),
      longFilePath_(logDirectory_ + "/telemetry_long.bin") {
    std::filesystem::create_directories(logDirectory_);
    RecoverUnclosedSession();
    RotateLongFileIfNeeded(kLongFileStartupMaxBytes);
    currentSessionStartMs_ = NowMs();
    WriteSessionStart(currentSessionStartMs_);
    sessionOpen_ = true;
}

TelemetryCollector::~TelemetryCollector() {
    if (!sessionOpen_) return;
    const auto endTs = lastSnapshot_.timestampMs > 0 ? lastSnapshot_.timestampMs : NowMs();
    WriteSessionEnd(currentSessionStartMs_, endTs);
    sessionOpen_ = false;
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
}

void TelemetryCollector::FlushMinuteAggregation() {
    std::deque<AggregatedSnapshot> source;
    {
        std::lock_guard<std::mutex> lock(binaryMutex_);
        if (tenSecRing_.size() < 6) return;
        source = tenSecRing_;
    }

    AggregatedSnapshot m;
    m.recordType = AggregatedRecordType::Metric;
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
    AppendLongRecord(m);
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
    return ReadCombinedLongRecords();
}

uint64_t TelemetryCollector::NowMs() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
    return static_cast<uint64_t>(now.time_since_epoch().count());
}

AggregatedSnapshot TelemetryCollector::AggregateWindow(const std::vector<Snapshot>& window) {
    AggregatedSnapshot out;
    if (window.empty()) return out;

    out.recordType = AggregatedRecordType::Metric;
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

void TelemetryCollector::AppendLongRecord(const AggregatedSnapshot& snap) {
    auto appendRaw = [this](const AggregatedSnapshot& rawSnap) {
        std::ofstream out(longFilePath_, std::ios::binary | std::ios::app);
        if (!out.is_open()) return;
        out.write(reinterpret_cast<const char*>(&rawSnap), sizeof(AggregatedSnapshot));
    };

    std::error_code ec;
    const bool exists = std::filesystem::exists(longFilePath_, ec) && !ec;
    const auto currentSize = exists ? std::filesystem::file_size(longFilePath_, ec) : 0;
    if (!ec && currentSize > kLongFileRuntimeRotateBytes && sessionOpen_) {
        const uint64_t endTs = lastSnapshot_.timestampMs > 0 ? lastSnapshot_.timestampMs : NowMs();

        AggregatedSnapshot endRecord;
        endRecord.recordType = AggregatedRecordType::SessionEnd;
        endRecord.windowStartMs = currentSessionStartMs_;
        endRecord.windowEndMs = endTs;
        endRecord.sessionDurationMs = endTs > currentSessionStartMs_ ? (endTs - currentSessionStartMs_) : 0;
        appendRaw(endRecord);

        if (RotateLongFileIfNeeded(kLongFileRuntimeRotateBytes)) {
            currentSessionStartMs_ = NowMs();
            AggregatedSnapshot startRecord;
            startRecord.recordType = AggregatedRecordType::SessionStart;
            startRecord.windowStartMs = currentSessionStartMs_;
            startRecord.windowEndMs = currentSessionStartMs_;
            appendRaw(startRecord);
        }
    }

    appendRaw(snap);
}

std::vector<AggregatedSnapshot> TelemetryCollector::ReadLongRecords(const std::string& filePath) const {
    std::vector<AggregatedSnapshot> records;
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) return records;

    AggregatedSnapshot s;
    while (in.read(reinterpret_cast<char*>(&s), sizeof(AggregatedSnapshot))) {
        records.push_back(s);
    }
    return records;
}

std::vector<AggregatedSnapshot> TelemetryCollector::ReadCombinedLongRecords() const {
    std::vector<AggregatedSnapshot> records;
    const auto rotatedPath = RotatedLongFilePath();

    auto previousRecords = ReadLongRecords(rotatedPath);
    records.insert(records.end(), previousRecords.begin(), previousRecords.end());

    auto currentRecords = ReadLongRecords(longFilePath_);
    records.insert(records.end(), currentRecords.begin(), currentRecords.end());
    return records;
}

bool TelemetryCollector::RotateLongFileIfNeeded(uintmax_t maxBytes) {
    std::error_code ec;
    if (!std::filesystem::exists(longFilePath_, ec) || ec) return false;

    const auto fileSize = std::filesystem::file_size(longFilePath_, ec);
    if (ec || fileSize <= maxBytes) return false;

    const auto rotatedPath = RotatedLongFilePath();
    std::filesystem::remove(rotatedPath, ec);
    ec.clear();

    std::filesystem::rename(longFilePath_, rotatedPath, ec);
    if (ec) return false;

    return true;
}

std::string TelemetryCollector::RotatedLongFilePath() const {
    return logDirectory_ + "/telemetry_long-1.bin";
}

void TelemetryCollector::WriteSessionStart(uint64_t tsMs) {
    AggregatedSnapshot s;
    s.recordType = AggregatedRecordType::SessionStart;
    s.windowStartMs = tsMs;
    s.windowEndMs = tsMs;
    AppendLongRecord(s);
}

void TelemetryCollector::WriteSessionEnd(uint64_t startTsMs, uint64_t endTsMs) {
    AggregatedSnapshot s;
    s.recordType = AggregatedRecordType::SessionEnd;
    s.windowStartMs = startTsMs;
    s.windowEndMs = endTsMs;
    s.sessionDurationMs = endTsMs > startTsMs ? (endTsMs - startTsMs) : 0;
    AppendLongRecord(s);
}

void TelemetryCollector::RecoverUnclosedSession() {
    auto records = ReadLongRecords(longFilePath_);
    if (records.empty()) return;

    int64_t lastStart = -1;
    for (int64_t i = static_cast<int64_t>(records.size()) - 1; i >= 0; --i) {
        if (records[static_cast<size_t>(i)].recordType == AggregatedRecordType::SessionEnd) {
            break;
        }
        if (records[static_cast<size_t>(i)].recordType == AggregatedRecordType::SessionStart) {
            lastStart = i;
            break;
        }
    }

    if (lastStart < 0) return;

    uint64_t endTs = records.back().windowEndMs;
    if (endTs == 0) endTs = records.back().windowStartMs;
    if (endTs == 0) endTs = NowMs();

    const uint64_t startTs = records[static_cast<size_t>(lastStart)].windowStartMs;
    WriteSessionEnd(startTs, endTs);
}

} // namespace Telemetry
