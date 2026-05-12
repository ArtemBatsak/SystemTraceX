#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <iomanip>
#include <map>

#include "src/collectors/cpu/cpu.h"
#include "src/collectors/ram/ram.h"
#include "src/collectors/disk/disk.h"
#include "src/collectors/systeminfo/systemInfo.h"
#include "src/collectors/errors/errors.h"
#include "src/collectors/network/network.h"
/*
#include "src/collectors/network/network.h"
#include "src/collectors/process/process.h"
*/

void update() {
    while (true) {
		CPU::Update();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}


int main()
{
	Net::NetworkCollector netCollector;
	auto updaterThread = std::thread(update);
    while (true) {
        
        auto cpu = CPU::GetSnapshot();
        auto ram = Memory::GetSnapshot();
		auto disk = Disk::GetSnapshot();
		auto sys = SystemInfo::GetSnapshot();
		auto errors = SystemErrors::GetSnapshot();

		auto net = netCollector.GetSnapshot();


        std::cout << "CPU Usage: " << std::fixed << std::setprecision(2) << cpu.totalUsage << "%, Cores: " << cpu.coreCount << ", Name: " << cpu.cpuname << std::endl;
        for (size_t i = 0; i < cpu.perCoreUsage.size(); ++i) {
            std::cout << "Core " << i << ": " << cpu.perCoreUsage[i] << "%" << std::endl;
        }
		std::cout << "===============================" << std::endl;
		std::cout << "Memory Usage: " << ram.usedRAM / (1024.0 * 1024 * 1024) << " GB / " << ram.totalRAM / (1024.0 * 1024 * 1024) << " GB" << std::endl;
		std::cout << "Swap Usage: " << ram.commitUsed / (1024.0 * 1024 * 1024) << " GB / " << ram.commitLimit / (1024.0 * 1024 * 1024) << " GB" << std::endl;
		std::cout << "===============================" << std::endl;
		for (const auto& d : disk.disks) {
			std::cout << "Disk: " << d.name << ", Free: " << d.freeBytes / (1024.0 * 1024 * 1024) << " GB, Total: " << d.totalBytes / (1024.0 * 1024 * 1024) << " GB" << std::endl;
		}
		std::cout << "===============================" << std::endl;
		std::cout << "System Info: " << std::endl;
		std::cout << "Hostname: " << sys.hostname << std::endl;
		std::cout << "OS: " << sys.osName << std::endl;
		std::cout << "Architecture: " << sys.architecture << std::endl;
		std::cout << "Uptime: " << sys.uptimeSeconds << " seconds" << std::endl;
		std::cout << "Virtualization: " << (sys.virtualization.hypervisorPresent ? "Yes" : "No") << std::endl;
		if (sys.virtualization.hypervisorPresent) {
			std::cout << "Hypervisor Vendor: " << sys.virtualization.vendor << std::endl;
			std::cout << "Running in VM: " << (sys.virtualization.runningInVM ? "Yes" : "No") << std::endl;
		}
		std::cout << "===============================" << std::endl;
		std::cout << "Error Events: " << std::endl;
		for (const auto& e : errors.lastEvents) {
			std::cout << "Timestamp: " << e.timestamp << ", Source: " << e.source << ", Severity: " << static_cast<int>(e.severity) << ", Message: " << e.message << ", Event ID: " << e.eventId << std::endl;
		}
		for (const auto& e : errors.criticalEvents) {
			std::cout << "CRITICAL - Timestamp: " << e.timestamp << ", Source: " << e.source << ", Message: " << e.message << ", Event ID: " << e.eventId << std::endl;
		}
		std::cout << " ===============================" << std::endl;
		std::cout << "Network: Total RX: " << net.totalRxPerSec << " B/s, Total TX: " << net.totalTxPerSec << " B/s" << std::endl;
		for (const auto& iface : net.interfaces) {
			std::cout << "Interface: " << iface.name << ", IP: " << iface.ipv4 << ", RX: " << iface.rxBytesPerSec << " B/s, TX: " << iface.txBytesPerSec << " B/s, Total RX: " << iface.rxTotalBytes / (1024.0 * 1024) << " MB, Total TX: " << iface.txTotalBytes / (1024.0 * 1024) << " MB, Loopback: " << (iface.isLoopback ? "Yes" : "No") << ", Up: " << (iface.isUp ? "Yes" : "No") << std::endl;
		}
		std::this_thread::sleep_for(std::chrono::seconds(5));
    }
	updaterThread.join();
    return 0;
}
