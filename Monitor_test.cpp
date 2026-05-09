#include <iostream>
#include "src/collectors/cpu/cpu.h"
#include "src/collectors/ram/ram.h"
#include "src/collectors/systeminfo/systemInfo.h"
#include <thread>


int main()
{
    
        std::cout << "CPU: " << CPU::GetCpuModel() << "\n";

        std::cout << "Cores: " << CPU::GetCoreCount() << "\n";
		std::cout << "Usage: " << CPU::GetUsage() << "%\n";
        auto core = CPU::GetPerCoreUsage();
        for(size_t i = 0; i < core.size(); i++)
        {
            std::cout << "Core " << i << ": " << core[i] << "%\n";
        }
		std::cout << "-----------------------------\n";
		std::cout << "ram: " << Memory::GetUsedRAM() / (1024 * 1024) << " MB / "
            << Memory::GetTotalRAM() / (1024 * 1024) << " MB\n";
        std::cout<< "swap: " << Memory::GetUsedSwap() / (1024 * 1024) << " MB / "
			<< Memory::GetTotalSwap() / (1024 * 1024) << " MB\n";


		std::cout << "=============================\n\n";
		std::cout << "System: " << SystemInfo::GetOSName() << " " << SystemInfo::GetKernelVersion() << "\n";
		std::cout << "Hostname: " << SystemInfo::GetHostname() << "\n";
		std::cout << "CPU: " << SystemInfo::GetCPUName() << "\n";
		std::cout << "Uptime: " << SystemInfo::GetUptimeSeconds() << " seconds\n";


        auto vm = SystemInfo::GetVirtualizationInfo();

        std::cout << "Hypervisor: "
            << (vm.hypervisorPresent ? "Yes" : "No")
            << "\n";

        std::cout << "Vendor: "
            << vm.vendor
            << "\n";

        std::cout << "Running in VM: "
            << (vm.runningInVM ? "Yes" : "No")
            << "\n";
		std::cout << "architecture: " << SystemInfo::GetArchitecture() << "\n";
        std::this_thread::sleep_for(
            std::chrono::milliseconds(5000));
    
    return 0;
}