# SystemTraceX

SystemTraceX is a lightweight cross-platform system monitoring and telemetry tool with real-time metrics collection.

## Documentation

- Collector module documentation: `src/collector/README.md`
- Web helper documentation: `src/web/README.md`
- Short collector overview: `Collectors.txt`

## Quick class setup

```cpp
Telemetry::TelemetryCollector collector("./telemetry_logs");
Web::WebTelemetryHelper webHelper(collector);
```
