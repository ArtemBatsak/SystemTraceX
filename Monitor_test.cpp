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
/*
#include "src/collectors/systeminfo/systemInfo.h"
#include "src/collectors/disk/disk.h"
#include "src/collectors/network/network.h"
#include "src/collectors/process/process.h"
#include "src/collectors/errors/errors.h"
*/

void update() {
    while (true) {
		CPU::Update();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}


int main()
{
	
	auto updaterThread = std::thread(update);
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto cpu = CPU::GetSnapshot();
        auto ram = Memory::GetSnapshot();
		auto disk = Disk::GetSnapshot();
		auto sys = SystemInfo::GetSnapshot();
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
    }
	updaterThread.join();
    return 0;
}
