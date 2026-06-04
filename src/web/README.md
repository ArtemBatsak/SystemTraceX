# Web Layer and Web Telemetry Helper

The web layer exposes SystemTraceX telemetry through a lightweight HTTP server. It serves the dashboard files, returns JSON for system metrics, and provides APIs for process tracking through `TaskLogger`.

## Main Components

| Component | File | Responsibility |
| --- | --- | --- |
| `WebTelemetryHelper` | `web/web_helper.h`, `web/web_helper.cpp` | Converts collector snapshots into JSON strings for the HTTP API. |
| `Web` | `web/web.h`, `web/web.cpp` | Owns the HTTP server, registers routes, serves static files, and wires process tracking endpoints to `TaskLogger`. |
| Frontend files | `web/index.html`, `web/script.js`, `web/tasksmanager.html`, `web/tasksmanager.js` | Browser UI for realtime telemetry and process tracking. |

## Setup

Create the collector first, then create the helper and web server:

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
}
```

`Web::Start()` blocks while the server is listening. Use `Web::Stop()` from another thread or shutdown path to stop the server.

## WebTelemetryHelper API

### `GetSnapshotString()`

Returns the latest full system snapshot as JSON.

Shape:

```json
{
  "cpu": {
    "name": "CPU name",
    "usage": 12.345
  },
  "disks": [
    {
      "free": 1000,
      "name": "C:\\",
      "total": 2000,
      "used": 1000
    }
  ],
  "gpus": [
    {
      "name": "GPU name",
      "usage": 3.5,
      "vramTotal": 8589934592,
      "vramUsed": 1073741824
    }
  ],
  "network": {
    "interfaces": [
      {
        "ipv4": "192.168.1.10",
        "isLoopback": false,
        "isUp": true,
        "name": "Ethernet",
        "rx": 1200.0,
        "rxTotal": 123456,
        "tx": 800.0,
        "txTotal": 654321
      }
    ],
    "rx": 1200.0,
    "tx": 800.0
  },
  "ram": {
    "free": 4000,
    "total": 8000,
    "used": 4000
  },
  "system": {
    "arch": "x64",
    "hostname": "machine",
    "kernel": "kernel version",
    "os": "Windows",
    "uptime": 12345,
    "virtualization": {
      "runningInVM": false,
      "vendor": "None"
    }
  },
  "time": 1710000000000
}
```

### `GetLiveWindowString()`

Returns the live ring buffer as a JSON array. Each item is a compact chart point.

Shape:

```json
[
  {
    "cpu": 12.3,
    "disks": [
      {
        "free": 1000,
        "name": "C:\\",
        "percent": 50.0,
        "total": 2000,
        "used": 1000
      }
    ],
    "disk": {
      "percent": 50.0
    },
    "gpu": 3.5,
    "network": {
      "rx": 1200.0,
      "tx": 800.0
    },
    "ram": {
      "percent": 50.0,
      "total": 8000,
      "used": 4000
    },
    "t": 1710000000000
  }
]
```

### `GetAggregatedWindowString(type)`

Returns aggregated history as a JSON array.

Supported values:

- `"24h"`: 10-second in-memory aggregates.
- `"long"`: long-range aggregates loaded from binary files.

Unknown values return `[]`.

Shape:

```json
[
  {
    "type": "Metric",
    "windowStartMs": 1710000000000,
    "windowEndMs": 1710000009999,
    "sessionDurationMs": 0,
    "cpu": {
      "avg": 12.0,
      "min": 10.0,
      "max": 15.0
    },
    "gpu": {
      "avg": 4.0,
      "min": 1.0,
      "max": 7.0
    },
    "ram": {
      "usedAvg": 4294967296,
      "usedMin": 4000000000,
      "usedMax": 4500000000
    }
  }
]
```

Record type can be `Metric`, `SessionStart`, `SessionEnd`, or `Unknown`.

### `GetProcessesString(count)`

Returns the latest process snapshot.

If `count` is `0`, the helper returns all process entries available in the collector snapshot.

Shape:

```json
{
  "totalProcesses": 120,
  "timestampMs": 1710000000000,
  "topProcesses": [
    {
      "pid": 1234,
      "name": "example.exe",
      "cpuUsage": 1.5,
      "memoryUsage": 104857600,
      "path": "C:\\Program Files\\Example\\example.exe",
      "importanceScore": 4.25
    }
  ]
}
```

### `GetErrorsString()`

Returns recent system error events as JSON.

Shape:

```json
{
  "lastEvents": [
    {
      "timestamp": 1710000000,
      "source": "OS",
      "severity": "Warning",
      "message": "Event message",
      "eventId": 1001
    }
  ],
  "criticalEvents": []
}
```

## HTTP Routes

### Pages and Assets

| Method | Route | Response |
| --- | --- | --- |
| `GET` | `/` | Serves `web/index.html`. |
| `GET` | `/tasks` | Serves the process tracking page. |
| `GET` | `/logs` | Serves the logs page if `web/logs.html` exists. |
| `GET` | `/script.js` | Serves dashboard JavaScript. |
| `GET` | `/taskmanager.js` | Serves process tracking JavaScript. |

### Telemetry API

| Method | Route | Description |
| --- | --- | --- |
| `GET` | `/api/snapshot` | Latest full system snapshot. |
| `GET` | `/api/telemetry/live` | Live raw snapshot window for realtime charts. |
| `GET` | `/api/telemetry/24h` | 10-second aggregated in-memory history. |
| `GET` | `/api/telemetry/long` | Long-range aggregated history loaded from disk. |
| `GET` | `/api/processes?count=40` | Process list limited to `count`; valid range is clamped to `0..2000`. |
| `GET` | `/api/processes/all` | Full process list available in the latest process snapshot. |

### Task Logger API

| Method | Route | Description |
| --- | --- | --- |
| `GET` | `/api/task-logger/tracked` | Returns tracked processes with latest metric point only. |
| `GET` | `/api/task-logger/history` | Returns tracked processes with latest metric point and ring-buffer history. |
| `GET` | `/api/task-logger/latest` | Returns only latest points by process path. |
| `POST` | `/api/task-logger/watch` | Adds or updates a tracked process. |
| `POST` | `/api/task-logger/enabled` | Enables or disables tracking for a path. |
| `POST` | `/api/task-logger/remove` | Removes a tracked process by path. |

## Task Logger Payloads

### Add or update a watched process

`POST /api/task-logger/watch`

```json
{
  "name": "Example",
  "path": "C:\\Program Files\\Example\\example.exe",
  "enabled": true
}
```

Response:

```json
{ "ok": true }
```

If `path` is missing, the server returns:

```json
{ "ok": false, "error": "path is required" }
```

### Enable or disable tracking

`POST /api/task-logger/enabled`

```json
{
  "path": "C:\\Program Files\\Example\\example.exe",
  "enabled": false
}
```

### Remove a watched process

`POST /api/task-logger/remove`

```json
{
  "path": "C:\\Program Files\\Example\\example.exe"
}
```

## Notes

- JSON is currently generated manually with `std::ostringstream`; all string fields should be passed through `EscapeJsonString()`.
- The helper returns strings rather than JSON objects, which keeps the web layer lightweight but makes schema changes manual.
- The server stores a local view of tracked processes from `tracked_processes.txt` and updates it when task logger routes mutate tracking state.
