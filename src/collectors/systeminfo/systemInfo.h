#pragma once

#include <string>
#include <cstdint>

namespace SystemInfo
{
    struct VirtualizationInfo
    {
        bool hypervisorPresent = false;
        bool runningInVM = false;

        std::string vendor = "None";
    };

    struct SystemSnapshot
    {
        std::string osName;
        std::string kernelVersion;
        std::string hostname;
        std::string cpuName;
        uint64_t uptimeSeconds = 0;
        std::string architecture;
        VirtualizationInfo virtualization;
    };
    SystemSnapshot GetSnapshot();

#ifdef _WIN32
    static std::string GetPreciseWindowsName();
    static std::string GetSystemArchitecture();
#endif
#ifdef __linux__
	std::string GetLinuxDistroName();   
#endif
}
