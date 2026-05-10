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

    std::string GetOSName();
    std::string GetKernelVersion();
    std::string GetHostname();
    std::string GetCPUName();

    uint64_t GetUptimeSeconds();
    std::string GetArchitecture();

    VirtualizationInfo GetVirtualizationInfo();
    SystemSnapshot GetSnapshot();
}
