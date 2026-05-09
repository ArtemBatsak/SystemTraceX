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

    std::string GetOSName();
    std::string GetKernelVersion();
    std::string GetHostname();
    std::string GetCPUName();

    uint64_t GetUptimeSeconds();
    std::string GetArchitecture();

    VirtualizationInfo GetVirtualizationInfo();
}