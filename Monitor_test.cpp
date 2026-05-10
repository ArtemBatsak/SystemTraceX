#include <chrono>
#include <iostream>
#include <thread>

#include "src/collectors/cpu/cpu.h"
#include "src/collectors/ram/ram.h"
#include "src/collectors/systeminfo/systemInfo.h"

#include "src/collectors/disk/disk.h"
#include "src/collectors/network/network.h"
#include "src/collectors/process/process.h"
#include "src/collectors/errors/errors.h"

template <typename Func>
auto MeasureMs(const char* name, Func&& fn)
{
    auto start = std::chrono::steady_clock::now();
    auto result = fn();
    auto end = std::chrono::steady_clock::now();

    auto durationMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "[time] " << name << ": " << durationMs << " ms\n";
    return result;
}

int main()
{
    // ================= CPU =================
    const auto cpuModel =
        MeasureMs("CPU::GetCpuModel", []() { return CPU::GetCpuModel(); });
    const auto cpuCoreCount =
        MeasureMs("CPU::GetCoreCount", []() { return CPU::GetCoreCount(); });
    const auto cpuSnapshot =
        MeasureMs("CPU::GetSnapshot", []() { return CPU::GetSnapshot(); });

    std::cout << "CPU: " << cpuModel << "\n";
    std::cout << "Cores: " << cpuCoreCount << "\n";
    std::cout << "Usage: " << cpuSnapshot.totalUsage << "%\n";

    for (size_t i = 0; i < cpuSnapshot.perCoreUsage.size(); i++)
        std::cout << "Core " << i << ": " << cpuSnapshot.perCoreUsage[i] << "%\n";

    std::cout << "-----------------------------\n";

    // ================= RAM =================
    const auto memorySnapshot =
        MeasureMs("Memory::GetSnapshot", []() { return Memory::GetSnapshot(); });

    std::cout << "RAM: "
        << memorySnapshot.usedRAM / (1024 * 1024) << " MB / "
        << memorySnapshot.totalRAM / (1024 * 1024) << " MB\n";

    std::cout << "Swap: "
        << memorySnapshot.commitUsed / (1024 * 1024) << " MB / "
        << memorySnapshot.commitLimit / (1024 * 1024) << " MB\n";

    std::cout << "=============================\n\n";

    // ================= SYSTEM INFO =================
    const auto systemSnapshot =
        MeasureMs("SystemInfo::GetSnapshot", []() { return SystemInfo::GetSnapshot(); });

    std::cout << "System: " << systemSnapshot.osName << " " << systemSnapshot.kernelVersion << "\n";
    std::cout << "Hostname: " << systemSnapshot.hostname << "\n";
    std::cout << "CPU Name: " << systemSnapshot.cpuName << "\n";
    std::cout << "Uptime: " << systemSnapshot.uptimeSeconds << " seconds\n";

    std::cout << "Hypervisor: " << (systemSnapshot.virtualization.hypervisorPresent ? "Yes" : "No") << "\n";
    std::cout << "Vendor: " << systemSnapshot.virtualization.vendor << "\n";
    std::cout << "Running in VM: " << (systemSnapshot.virtualization.runningInVM ? "Yes" : "No") << "\n";
    std::cout << "Architecture: " << systemSnapshot.architecture << "\n";

    std::cout << "-----------------------------\n";

    // ================= DISK =================
    auto disk = MeasureMs("Disk::GetSnapshot", []() {
        return Disk::GetSnapshot();
        });

    std::cout << "DISK:\n";
    for (auto& d : disk.disks)
    {
        std::cout << "  Disk read: " << d.readBytesPerSec / (1024.0 * 1024.0)
            << " MB/s write: " << d.writeBytesPerSec / (1024.0 * 1024.0)
            << " MB/s\n";
    }

    // ================= NETWORK =================
    auto net = MeasureMs("Net::GetSnapshot", []() {
        return Net::GetSnapshot();
        });

    std::cout << "NETWORK:\n";
    std::cout << "  RX: " << net.totalRxPerSec / (1024.0 * 1024.0) << " MB/s\n";
    std::cout << "  TX: " << net.totalTxPerSec / (1024.0 * 1024.0) << " MB/s\n";

    for (auto& i : net.interfaces)
    {
        std::cout << "  IF " << i.name
            << " RX: " << i.rxBytesPerSec
            << " TX: " << i.txBytesPerSec << "\n";
    }

    std::cout << "-----------------------------\n";

    // ================= PROCESS =================
    auto proc = MeasureMs("Process::GetSnapshot", []() {
        return Process::GetSnapshot();
        });

    std::cout << "Processes: " << proc.processCount << "\n";

    std::cout << "\nTop CPU:\n";
    for (auto& p : proc.topCpu)
        std::cout << "  " << p.name << " CPU: " << p.cpuUsage << "%\n";

    std::cout << "\nTop Memory:\n";
    for (auto& p : proc.topMemory)
        std::cout << "  " << p.name << " RAM: "
        << p.memoryBytes / (1024 * 1024) << " MB\n";

    std::cout << "\nTop Disk:\n";
    for (auto& p : proc.topDisk)
        std::cout << "  " << p.name << " IO: "
        << (p.diskReadBytes + p.diskWriteBytes) / 1024 << " KB\n";

    std::cout << "-----------------------------\n";

    // ================= ERRORS =================
    auto errors = MeasureMs("SystemErrors::GetSnapshot", []() {
        return SystemErrors::GetSnapshot();
        });

    std::cout << "Errors: " << errors.errorCount << "\n";
    std::cout << "Warnings: " << errors.warningCount << "\n";
    std::cout << "Critical: " << errors.criticalCount << "\n";

    for (auto& e : errors.lastErrors)
    {
        std::cout << "["
            << (int)e.severity << "] "
            << e.message
            << " (pid: " << e.pid << ")\n";
    }

    std::cout << "=============================\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));

    return 0;
}
