# Web Helper Guide

This README explains how to use `Web::WebTelemetryHelper`.

## Purpose

`WebTelemetryHelper` is a thin wrapper over `Telemetry::TelemetryCollector` for web/API layers (REST/WebSocket/UI).

It provides:

- `GetCurrentSnapshot()`
- `GetLiveGraphBootstrap()`
- `Get24HoursSeries()`
- `GetLongRangeSeries()`

## Class declarations

```cpp
Telemetry::TelemetryCollector collector("./telemetry_logs");
Web::WebTelemetryHelper webHelper(collector);
```

## Basic requirement

Create the collector before the web helper. The collector starts background
collection automatically:

```cpp
Telemetry::TelemetryCollector collector("./telemetry_logs");
Web::WebTelemetryHelper webHelper(collector);
```

After that, `GetCurrentSnapshot()` reads the latest data collected in the
background.

## Get current snapshot

```cpp
auto current = webHelper.GetCurrentSnapshot();
```

Available blocks:

- `current.cpu`
- `current.memory`
- `current.disk`
- `current.network`
- `current.system`

## Example: read all main blocks

```cpp
auto current = webHelper.GetCurrentSnapshot();

// CPU
std::cout << "CPU total usage: " << current.cpu.totalUsage << "%\n";
for (size_t i = 0; i < current.cpu.perCoreUsage.size(); ++i) {
    std::cout << "  Core " << i << ": " << current.cpu.perCoreUsage[i] << "%\n";
}

// RAM
std::cout << "RAM: used=" << current.memory.usedRAM / (1024.0 * 1024.0)
          << " MB, free=" << current.memory.freeRAM / (1024.0 * 1024.0) << " MB\n";

// DISK
auto disks = current.disk.disks;
for (const auto& disk : disks) {
    std::cout << "  Disk " << disk.name << ": "
              << disk.freeBytes / (1024.0 * 1024.0) << " MB free / "
              << disk.totalBytes / (1024.0 * 1024.0) << " MB total\n";
}

// NETWORK
std::cout << "Net RX: " << current.network.totalRxPerSec << " B/s\n";
std::cout << "Net TX: " << current.network.totalTxPerSec << " B/s\n";
for (const auto& nic : current.network.interfaces) {
    std::cout << "  IF " << nic.name << " (" << nic.ipv4 << ")"
              << " RX=" << nic.rxBytesPerSec << " B/s"
              << " TX=" << nic.txBytesPerSec << " B/s"
              << " up=" << nic.isUp
              << " loopback=" << nic.isLoopback << "\n";
}

// SYSTEM
std::cout << "OS: " << current.system.osName << "\n";
std::cout << "Kernel: " << current.system.kernelVersion << "\n";
std::cout << "Host: " << current.system.hostname << "\n";
std::cout << "Uptime: " << current.system.uptimeSeconds << " sec\n";
```

## Historical series for charts

- `GetLiveGraphBootstrap()` — live snapshots for initial realtime chart bootstrap.
- `Get24HoursSeries()` — 10-second aggregates.
- `GetLongRangeSeries()` — minute aggregates.

```cpp
auto live = webHelper.GetLiveGraphBootstrap();
auto day = webHelper.Get24HoursSeries();
auto longRange = webHelper.GetLongRangeSeries();
```
