# SystemTraceX

SystemTraceX is a lightweight system monitoring application with a built-in web interface, realtime telemetry, historical aggregation, process inspection, and focused process logging.

It is designed for local operational visibility: start the collector, open the dashboard, and watch CPU, GPU, memory, disk, network, system, and process data update live.

## Highlights

- Lightweight C++ telemetry core.
- Web dashboard served by the application itself.
- Realtime system snapshots updated every second.
- Live chart bootstrap from the in-memory telemetry window.
- 24-hour in-memory aggregation at 10-second resolution.
- Long-range binary telemetry history with session markers and file rotation.
- Process table with CPU, memory, path, PID, and importance score.
- Process tracking logger for selected applications.
- Cross-platform collector structure with Windows and Linux implementations in several modules.

## Architecture

```text
collectors/*
    CPU, GPU, RAM, disk, network, system info, errors, and process collectors

collector/
    TelemetryCollector: unified snapshots, live window, aggregation, long-range storage

func/
    TaskLogger and utility functions

web/
    WebTelemetryHelper, HTTP server, dashboard pages, and frontend scripts
```

## Runtime Flow

1. `Telemetry::TelemetryCollector` starts a background worker.
2. Every second it captures a full system snapshot and a process snapshot.
3. Raw snapshots are stored in a live ring buffer.
4. Every 10 seconds the collector creates an in-memory aggregate.
5. Every minute it writes a long-range aggregate to disk.
6. `WebTelemetryHelper` converts collector data into JSON.
7. `Web` exposes dashboards and API endpoints.
8. `TaskLogger` records history for user-selected process paths.

## Core Modules

### Collector

The collector is the heart of the application. It combines all low-level collector modules into one telemetry stream.

Main API:

- `GetLastSnapshot()`
- `GetLiveWindow()`
- `GetRecent24Hours()`
- `GetLongRange()`
- `GetLastProcessSnapshot()`

Detailed documentation: [collector/README.md](collector/README.md)

### Web and Web Helper

The web layer serves the UI and exposes JSON endpoints for dashboards and process tracking.

Main endpoints:

- `GET /api/snapshot`
- `GET /api/telemetry/live`
- `GET /api/telemetry/24h`
- `GET /api/telemetry/long`
- `GET /api/processes`
- `GET /api/task-logger/tracked`
- `POST /api/task-logger/watch`

Detailed documentation: [web/README.md](web/README.md)

### Task Logger

`TaskLogger` tracks selected executable paths and stores compact per-process history in memory.

It records:

- Timestamp
- CPU usage
- RAM usage
- Running instance count

Detailed documentation: [func/README.md](func/README.md)

## Example Integration

```cpp
#include "collector/collector.h"
#include "web/web_helper.h"
#include "web/web.h"
#include "func/funk.h"

int main() {
    Telemetry::TelemetryCollector collector("./telemetry_logs");
    WebTelemetryHelper helper(collector);
    TaskLogger taskLogger(collector);

    taskLogger.start();

    Web web(helper, taskLogger);
    web.Start("0.0.0.0", 8080);

    return 0;
}
```

Open the dashboard at:

```text
http://localhost:8080/
```

## Telemetry Data

SystemTraceX collects:

- CPU usage and CPU identity.
- GPU adapters, usage, VRAM, and vendor details.
- RAM totals and current usage.
- Disk capacity and free space.
- Network throughput and interface metadata.
- OS, kernel, hostname, uptime, architecture, and virtualization status.
- Process list with PID, name, path, CPU, RAM, and importance score.
- Recent and critical system errors where supported.

## Storage

The collector writes long-range telemetry as binary `AggregatedSnapshot` records.

Default long-range files:

- `telemetry_long.bin`
- `telemetry_long-1.bin`

Tracked process configuration is stored in:

```text
tracked_processes.txt
```

Process metric history is currently in memory only.

## Production Notes

SystemTraceX is built around a small memory footprint and direct local telemetry collection. It avoids an external database for core monitoring and keeps the HTTP layer simple, making it suitable for local diagnostics, lightweight dashboards, development machines, and compact operational tools.

Recommended next steps for hardening:

- Add a build manifest such as CMake or a Visual Studio project if one is not maintained outside this folder.
- Replace manual JSON string construction with structured JSON serialization for every API response.
- Add route tests for the web API schemas.
- Add graceful handling for missing static files.
- Consider persistent history for `TaskLogger` metric samples if process tracking should survive restarts.
