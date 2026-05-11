#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

#include "src/collectors/cpu/cpu.h"
#include "src/collectors/ram/ram.h"
#include "src/collectors/systeminfo/systemInfo.h"
#include "src/collectors/disk/disk.h"
#include "src/collectors/network/network.h"
#include "src/collectors/process/process.h"
#include "src/collectors/errors/errors.h"

struct FullSystemSnapshot
{
    CPU::CpuSnapshot cpu;
    Memory::MemorySnapshot memory;
    SystemInfo::SystemSnapshot system;
    Disk::DiskSystemSnapshot disk;
    Net::NetworkSnapshot network;
    Process::ProcessSnapshot process;
    SystemErrors::ErrorSnapshot errors;
    std::chrono::system_clock::time_point timestamp;
};

static std::mutex g_snapshotMutex;
static FullSystemSnapshot g_snapshot;

static void CollectLoop(std::atomic<bool>& running)
{
    while (running.load())
    {
        FullSystemSnapshot next;
        next.cpu = CPU::GetSnapshot();
        next.memory = Memory::GetSnapshot();
        next.system = SystemInfo::GetSnapshot();
        Disk::Update();
        next.disk = Disk::GetSnapshot();
        Net::Update();
        next.network = Net::GetSnapshot();
        Process::Update();
        next.process = Process::GetSnapshot();
        next.errors = SystemErrors::GetSnapshot();
        next.timestamp = std::chrono::system_clock::now();

        {
            std::lock_guard<std::mutex> lock(g_snapshotMutex);
            g_snapshot = std::move(next);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

static void PrintLoop(std::atomic<bool>& running)
{
    while (running.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(20));

        FullSystemSnapshot snap;
        {
            std::lock_guard<std::mutex> lock(g_snapshotMutex);
            snap = g_snapshot;
        }

        std::cout << "\n=== FULL SNAPSHOT ===\n";
        std::cout << "CPU total: " << snap.cpu.totalUsage << "%\n";
        std::cout << "RAM: " << snap.memory.usedRAM / (1024 * 1024) << " MB / "
                  << snap.memory.totalRAM / (1024 * 1024) << " MB\n";
        std::cout << "OS: " << snap.system.osName << " " << snap.system.kernelVersion << "\n";
        std::cout << "Processes: " << snap.process.processCount << "\n";

        std::cout << "Top CPU:\n";
        for (const auto& p : snap.process.topCpu)
            std::cout << "  " << p.pid << " " << p.name << " CPU: " << p.cpuUsage << "%\n";

        std::cout << "Top Disk:\n";
        for (const auto& p : snap.process.topDisk)
            std::cout << "  " << p.pid << " " << p.name << " IO: "
                      << (p.diskReadBytes + p.diskWriteBytes) / 1024 << " KB/s\n";

        std::cout << "=====================\n";
    }
}

int main()
{
    std::atomic<bool> running{ true };

    std::thread collector(CollectLoop, std::ref(running));
    std::thread printer(PrintLoop, std::ref(running));

    std::this_thread::sleep_for(std::chrono::minutes(2));
    running.store(false);

    collector.join();
    printer.join();

    return 0;
}
