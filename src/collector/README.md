# Collector Module

`Telemetry::TelemetryCollector` is the central data pipeline for SystemTraceX. It reads raw system metrics from the collector modules, keeps a short live history in memory, builds aggregated history, and records long-range telemetry to binary files.

## What It Collects

The main `Telemetry::Snapshot` contains:

| Field | Source | Description |
| --- | --- | --- |
| `timestampMs` | Collector clock | Unix timestamp in milliseconds. |
| `cpu` | `CPU::GetSnapshot()` | CPU name, total usage, per-core usage, and core count. |
| `gpu` | `GPU::GetSnapshots()` | Detected GPUs, usage, VRAM values, vendor, and adapter flags. |
| `memory` | `Memory::GetSnapshot()` | Total, used, and free RAM; commit/swap fields where available. |
| `disk` | `Disk::GetSnapshot()` | Disk names, total bytes, and free bytes. |
| `network` | `Net::GetSnapshot()` | Per-interface and total RX/TX rates. |
| `system` | `SystemInfo::GetSnapshot()` | OS, kernel, hostname, uptime, architecture, and virtualization data. |

The collector also stores the latest process snapshot from `Proc::GetSnapshot(0)` so the web layer and process logger can read process telemetry.

## Lifecycle

Create a collector with a log directory:

```cpp
#include "collector/collector.h"

Telemetry::TelemetryCollector collector("./telemetry_logs");
```

The constructor:

1. Creates the log directory if it does not exist.
2. Recovers the previous session if the application exited without writing `SessionEnd`.
3. Rotates the long-range telemetry file if it is too large.
4. Writes a `SessionStart` record.
5. Starts the background worker thread.

The destructor stops the worker thread and writes a `SessionEnd` record.

## Background Worker

The worker runs once per second:

1. Calls `CollectRawSnapshot()`.
2. Calls `Proc::GetSnapshot(0)` to capture the full process list.
3. Updates the latest snapshot and the live ring buffer.
4. Every 10 ticks, calls `FlushTenSecondAggregation()`.
5. Every 60 ticks, calls `FlushMinuteAggregation()`.

The live buffer capacity is `kLiveCapacity` (`600` samples), which is approximately 10 minutes at one sample per second.

The 10-second aggregation ring capacity is `kTenSecCapacity` (`8640` records), which is approximately 24 hours.

## Public API

### `CollectRawSnapshot()`

Collects one immediate `Telemetry::Snapshot`.

This method is used internally by the worker, but it can also be called directly when a one-off snapshot is needed.

```cpp
Telemetry::Snapshot snapshot = collector.CollectRawSnapshot();
```

### `GetLastSnapshot()`

Returns the most recent live snapshot.

```cpp
auto snapshot = collector.GetLastSnapshot();
double cpu = snapshot.cpu.totalUsage;
uint64_t usedRam = snapshot.memory.usedRAM;
```

### `GetLiveWindow()`

Returns the current live ring as a vector of raw snapshots.

```cpp
std::vector<Telemetry::Snapshot> live = collector.GetLiveWindow();
```

Use this for realtime charts, dashboard bootstrapping, and short rolling analysis.

### `GetRecent24Hours()`

Returns the in-memory 10-second aggregated records.

```cpp
std::vector<Telemetry::AggregatedSnapshot> day = collector.GetRecent24Hours();
```

Each record contains average/min/max values for CPU, GPU, RAM usage, and network throughput over its aggregation window.

### `GetLongRange()`

Reads long-range records from disk and returns the combined history.

```cpp
std::vector<Telemetry::AggregatedSnapshot> history = collector.GetLongRange();
```

Records are read from `telemetry_long-1.bin` first, then `telemetry_long.bin`, preserving chronological order across the current and rotated file.

### `GetLastProcessSnapshot()`

Returns the latest process snapshot collected by the worker.

```cpp
Proc::ProcessSnapshot processes = collector.GetLastProcessSnapshot();
```

Use this for process tables, process search, or the process logger.

## Aggregated Records

`Telemetry::AggregatedSnapshot` can represent three record types:

| Type | Meaning |
| --- | --- |
| `Metric` | A telemetry aggregate for a time window. |
| `SessionStart` | Marks when the application started a collection session. |
| `SessionEnd` | Marks when the collection session ended and stores `sessionDurationMs`. |

Metric records include:

- `windowStartMs`, `windowEndMs`
- `cpuAvg`, `cpuMin`, `cpuMax`
- `gpuAvg`, `gpuMin`, `gpuMax`
- `ramPercentAvg`
- `diskPercentAvg`
- `ramUsedAvg`, `ramUsedMin`, `ramUsedMax`
- `netRxAvg`, `netRxMin`, `netRxMax`
- `netTxAvg`, `netTxMin`, `netTxMax`

## Storage

Long-range telemetry is stored as raw binary `AggregatedSnapshot` records.

Files:

- `telemetry_long.bin`: active long-range file.
- `telemetry_long-1.bin`: previous rotated file.

Rotation limits:

- Startup rotation: `kLongFileStartupMaxBytes` (`1 MB`).
- Runtime rotation: `kLongFileRuntimeRotateBytes` (`2 MB`).

During runtime rotation, the collector writes a `SessionEnd`, rotates the file, starts a new session, and then continues writing metrics.

## Thread Safety

The collector uses:

- `std::shared_mutex` for live snapshot reads/writes.
- `std::mutex` for binary aggregation and long-range file operations.
- `std::atomic<bool>` to control the worker thread.

The public getters return copies, so callers can safely use the returned values without holding collector locks.

## Minimal Integration Example

```cpp
#include "collector/collector.h"
#include "web/web_helper.h"
#include "func/funk.h"

int main() {
    Telemetry::TelemetryCollector collector("./telemetry_logs");
    WebTelemetryHelper webHelper(collector);
    TaskLogger taskLogger(collector);

    taskLogger.start();

    auto snapshot = collector.GetLastSnapshot();
    auto live = collector.GetLiveWindow();
    auto processes = collector.GetLastProcessSnapshot();

    return 0;
}
```
