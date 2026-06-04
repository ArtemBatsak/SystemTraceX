# Function Utilities: TaskLogger

This document covers the process logging part of `func/funk.h` and `func/funk.cpp`.

`TaskLogger` tracks selected processes over time. It reads the latest process snapshot from `Telemetry::TelemetryCollector`, aggregates matching process instances by executable path, and stores a compact in-memory history for each tracked process.

## Data Types

### `MetricPoint`

One metric sample for a tracked process path.

| Field | Type | Description |
| --- | --- | --- |
| `timestamp` | `uint64_t` | Snapshot timestamp in milliseconds. |
| `cpu` | `float` | Total CPU usage across all matching process instances. |
| `ram` | `uint64_t` | Total RAM usage across all matching process instances, in bytes. |
| `instances` | `uint16_t` | Number of running process instances that matched the tracked path. |

### `ProcessRing`

A fixed-size ring buffer for `MetricPoint` values.

`HISTORY_SIZE` is `100`, so each tracked process keeps up to 100 samples.

Methods:

- `addEntry(const MetricPoint& entry)`: appends one sample.
- `getLatest()`: returns the newest sample, or an empty point if no data exists.
- `getAll()`: returns all valid samples in chronological order.

### `TrackedProcess`

Configuration for a watched process.

| Field | Type | Description |
| --- | --- | --- |
| `name` | `std::string` | Display name. |
| `path` | `std::string` | Executable path used as the tracking key. |
| `enable` | `bool` | Whether this process should be sampled. |

## TaskLogger Lifecycle

Create the telemetry collector first:

```cpp
Telemetry::TelemetryCollector collector("./telemetry_logs");
TaskLogger taskLogger(collector);
```

The constructor loads tracked processes from the save file. By default, the file is:

```text
tracked_processes.txt
```

Start the logger thread explicitly:

```cpp
taskLogger.start();
```

The destructor stops the logger thread, joins it, and saves the tracked process list.

## How Sampling Works

`TaskLogger::updateLoop()` runs once per second:

1. Reads `collector_.GetLastProcessSnapshot()`.
2. Passes the snapshot to `processSnapshot()`.
3. Sleeps for one second.

For each enabled tracked process, `processSnapshot()` scans `snapshot.topProcesses` and matches entries by exact `path`.

When one or more instances match:

- CPU usage is summed.
- RAM usage is summed.
- Instance count is incremented.
- One `MetricPoint` is appended to the process ring buffer.

If no process instance matches the tracked path, no new point is written for that tick.

## Public API

### `addProcessToTrack(name, path, enable)`

Adds a tracked process or replaces the existing configuration for the same path.

```cpp
taskLogger.addProcessToTrack(
    "Example",
    "C:\\Program Files\\Example\\example.exe",
    true
);
```

### `removeProcessToTrack(path)`

Removes the tracked process and its in-memory history.

```cpp
taskLogger.removeProcessToTrack("C:\\Program Files\\Example\\example.exe");
```

### `setTrackEnabled(path, enable)`

Enables or disables sampling for an existing tracked process.

```cpp
taskLogger.setTrackEnabled("C:\\Program Files\\Example\\example.exe", false);
```

### `getProcessHistoryByPath(path)`

Returns all stored points for a tracked executable path.

```cpp
std::vector<MetricPoint> history =
    taskLogger.getProcessHistoryByPath("C:\\Program Files\\Example\\example.exe");
```

### `getLatestProcessPointByPath(path)`

Returns the latest point for a tracked executable path.

```cpp
MetricPoint latest =
    taskLogger.getLatestProcessPointByPath("C:\\Program Files\\Example\\example.exe");
```

### `getProcessHistoryByName(name)`

Finds the first tracked process with the matching display name and returns its history.

```cpp
auto history = taskLogger.getProcessHistoryByName("Example");
```

Path-based lookups are more reliable when multiple tracked items can share the same display name.

### `getLatestProcessPointByName(name)`

Finds the first tracked process with the matching display name and returns its latest point.

```cpp
MetricPoint latest = taskLogger.getLatestProcessPointByName("Example");
```

### `saveTrackedProcesses()`

Writes the tracked process list to disk.

```cpp
taskLogger.saveTrackedProcesses();
```

File format:

```text
name|path|enabled
```

Example:

```text
Example|C:\Program Files\Example\example.exe|1
```

### `loadTrackedProcesses()`

Loads tracked process configuration from disk.

```cpp
taskLogger.loadTrackedProcesses();
```

Malformed lines are ignored. Existing tracked configuration is cleared before loading.

## Thread Safety

`TaskLogger` protects tracking configuration and history with a single `std::mutex`.

All public read and write operations lock the mutex, so callers receive copies that are safe to use after the call returns.

## Usage Example

```cpp
#include "collector/collector.h"
#include "func/funk.h"

int main() {
    Telemetry::TelemetryCollector collector("./telemetry_logs");
    TaskLogger logger(collector);

    logger.addProcessToTrack(
        "Example",
        "C:\\Program Files\\Example\\example.exe",
        true
    );

    logger.start();

    auto latest = logger.getLatestProcessPointByPath(
        "C:\\Program Files\\Example\\example.exe"
    );

    return 0;
}
```

## Operational Notes

- Tracking is path-based. If a process entry has an empty path, it will not match path-based tracked processes.
- The logger only stores samples while the process is running and visible in the collector process snapshot.
- History is in memory only. The saved file persists the tracked process list, not metric history.
- The default ring buffer stores 100 points per process.
