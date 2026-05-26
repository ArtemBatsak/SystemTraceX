# Collector Module Guide

This README explains:

1. What data is produced by all collector modules (`src/collectors/*`),
2. How the main collector `Telemetry::TelemetryCollector` works (`src/collector/collector.h`).

## 1) Data produced by each collector

The main `Telemetry::Snapshot` contains 5 blocks:

- `cpu` from `CPU::GetSnapshot()`
- `memory` from `Memory::GetSnapshot()`
- `disk` from `Disk::GetSnapshot()`
- `network` from `Net::GetSnapshot()`
- `system` from `SystemInfo::GetSnapshot()`

### CPU collector (`src/collectors/cpu`)

`CPU::CpuSnapshot` fields:

- `totalUsage` — total CPU usage in percent,
- `perCoreUsage` — per-core usage values,
- `coreCount` — number of cores,
- `cpuname` — CPU model/name.

Important: `CPU::Update()` is called before reading the CPU snapshot.

### RAM collector (`src/collectors/ram`)

`Memory::MemorySnapshot` fields:

- `totalRAM` — total RAM,
- `usedRAM` — used RAM,
- `freeRAM` — free RAM,
- `commitLimit` — commit/swap limit,
- `commitUsed` — used commit/swap.

### Disk collector (`src/collectors/disk`)

`Disk::DiskSystemSnapshot` fields:

- `disks` — list of `Disk::DiskSnapshot`:
  - `name` — disk name/mount,
  - `freeBytes` — free bytes,
  - `totalBytes` — total bytes.

### Network collector (`src/collectors/network`)

`Net::NetworkSnapshot` fields:

- `totalRxPerSec` — total incoming traffic (B/s),
- `totalTxPerSec` — total outgoing traffic (B/s),
- `interfaces` — list of `InterfaceSnapshot`:
  - `name`, `ipv4`,
  - `rxBytesPerSec`, `txBytesPerSec`,
  - `rxTotalBytes`, `txTotalBytes`,
  - `isLoopback`, `isUp`.

### SystemInfo collector (`src/collectors/systeminfo`)

`SystemInfo::SystemSnapshot` fields:

- `osName`, `kernelVersion`, `hostname`,
- `cpuName`, `uptimeSeconds`, `architecture`,
- `virtualization`:
  - `hypervisorPresent`,
  - `runningInVM`,
  - `vendor`.

---

## 2) Main collector: `Telemetry::TelemetryCollector`

`TelemetryCollector` builds a unified `Snapshot`, keeps a live window, and stores aggregated history.

### Class declaration and helper wiring

```cpp
Telemetry::TelemetryCollector collector("./telemetry_logs");
Web::WebTelemetryHelper webHelper(collector);
```

### Core workflow

Creating the collector starts its background update thread automatically:

```cpp
Telemetry::TelemetryCollector collector("./telemetry_logs");
```

After that, other threads can read data through getters without manually driving
the collector loop.

### Key methods

- `CollectRawSnapshot()`
  - Creates `Telemetry::Snapshot` and fills `timestampMs`,
  - calls CPU/RAM/Disk/Network/SystemInfo collectors,
  - returns one combined snapshot.

- `PushLiveSnapshot(Snapshot snapshot)`
  - Updates `lastSnapshot_`,
  - appends to `liveRing_`,
  - keeps at most `kLiveCapacity` entries.

- `FlushTenSecondAggregation()`
  - Uses the latest 10 live snapshots,
  - computes avg/min/max for CPU, RAM, RX, TX,
  - pushes to `tenSecRing_`,
  - writes `telemetry_10s.bin`.

- `FlushMinuteAggregation()`
  - Uses the latest six 10-second aggregates,
  - computes 1-minute avg/min/max aggregate,
  - appends binary record to `telemetry_long.bin`.

- Long-range file rotation:
  - On startup, if `telemetry_long.bin` is larger than 1 MB, collector removes
    previous `telemetry_long-1.bin` (if present), renames current file to
    `telemetry_long-1.bin`, and starts writing to a fresh `telemetry_long.bin`.
  - During runtime, before appending next record, if `telemetry_long.bin` is
    larger than 2 MB, collector forcibly closes the current session with
    `SessionEnd`, rotates file to `telemetry_long-1.bin`, writes `SessionStart`
    to a new `telemetry_long.bin`, then continues normal writes.

- Read methods:
  - `GetLastSnapshot()` — last live snapshot,
  - `GetLiveWindow()` — live ring as vector,
  - `GetRecent24Hours()` — 10-second aggregate series,
  - `GetLongRange()` — minute aggregate series merged from
    `telemetry_long-1.bin` first and `telemetry_long.bin` second.

### Short loop example

```cpp
#include "collector.h"
#include "../web/web_helper.h"

int main() {
    Telemetry::TelemetryCollector collector("./telemetry_logs");
    Web::WebTelemetryHelper webHelper(collector);

    auto current = collector.GetLastSnapshot();
    auto live = collector.GetLiveWindow();
}
```
