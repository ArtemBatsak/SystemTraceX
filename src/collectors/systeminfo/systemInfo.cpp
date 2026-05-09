#ifdef __linux__

#include "systemInfo.h"

#include <fstream>
#include <string>

#include <unistd.h>
#include <sys/utsname.h>

namespace SystemInfo
{
    static std::string ReadFile(const std::string& path)
    {
        std::ifstream file(path);

        if (!file.is_open())
            return "";

        std::string value;

        std::getline(file, value);

        return value;
    }

    std::string GetOSName()
    {
        std::ifstream file("/etc/os-release");

        std::string line;

        while (std::getline(file, line))
        {
            if (line.find("PRETTY_NAME=") == 0)
            {
                auto pos = line.find("=");

                std::string val =
                    line.substr(pos + 1);

                if (!val.empty() && val.front() == '"')
                    val.erase(0, 1);

                if (!val.empty() && val.back() == '"')
                    val.pop_back();

                return val;
            }
        }

        return "Linux";
    }

    std::string GetKernelVersion()
    {
        struct utsname buffer;

        uname(&buffer);

        return buffer.release;
    }

    std::string GetHostname()
    {
        char buf[256];

        gethostname(buf, sizeof(buf));

        return std::string(buf);
    }

    std::string GetCPUName()
    {
        std::ifstream file("/proc/cpuinfo");

        std::string line;

        while (std::getline(file, line))
        {
            if (line.find("model name") != std::string::npos)
            {
                return line.substr(
                    line.find(":") + 2
                );
            }
        }

        return "Unknown CPU";
    }

    uint64_t GetUptimeSeconds()
    {
        std::ifstream file("/proc/uptime");

        double uptime = 0;

        file >> uptime;

        return (uint64_t)uptime;
    }

    std::string GetArchitecture()
    {
        return sizeof(void*) == 8 ? "x64" : "x86";
    }

    // ------------------------------------------------
    // Virtualization
    // ------------------------------------------------

    VirtualizationInfo GetVirtualizationInfo()
    {
        VirtualizationInfo info;

        std::string vendor =
            ReadFile("/sys/class/dmi/id/sys_vendor");

        std::string product =
            ReadFile("/sys/class/dmi/id/product_name");

        // WSL2 detection
        struct utsname uts;

        uname(&uts);

        std::string release = uts.release;

        if (
            release.find("WSL") != std::string::npos ||
            release.find("microsoft") != std::string::npos
            )
        {
            info.hypervisorPresent = true;
            info.runningInVM = true;
            info.vendor = "WSL2";

            return info;
        }

        // VMware
        if (
            vendor.find("VMware") != std::string::npos ||
            product.find("VMware") != std::string::npos
            )
        {
            info.hypervisorPresent = true;
            info.runningInVM = true;
            info.vendor = "VMware";
        }

        // VirtualBox
        else if (
            vendor.find("VirtualBox") != std::string::npos ||
            product.find("VirtualBox") != std::string::npos
            )
        {
            info.hypervisorPresent = true;
            info.runningInVM = true;
            info.vendor = "VirtualBox";
        }

        // KVM / QEMU
        else if (
            vendor.find("QEMU") != std::string::npos ||
            product.find("KVM") != std::string::npos ||
            product.find("QEMU") != std::string::npos
            )
        {
            info.hypervisorPresent = true;
            info.runningInVM = true;
            info.vendor = "KVM/QEMU";
        }

        // Hyper-V
        else if (
            vendor.find("Microsoft") != std::string::npos &&
            product.find("Virtual Machine") != std::string::npos
            )
        {
            info.hypervisorPresent = true;
            info.runningInVM = true;
            info.vendor = "Hyper-V";
        }

        // Xen
        else if (
            vendor.find("Xen") != std::string::npos ||
            product.find("HVM domU") != std::string::npos
            )
        {
            info.hypervisorPresent = true;
            info.runningInVM = true;
            info.vendor = "Xen";
        }

        return info;
    }
}

#endif

#ifdef _WIN32

#include "systemInfo.h"

#include <windows.h>
#include <winreg.h>
#include <intrin.h>

#include <string>

namespace SystemInfo
{
    std::string GetOSName()
    {
        return "Windows";
    }

    std::string GetKernelVersion()
    {
        HKEY hKey;

        char buffer[256];
        DWORD size = sizeof(buffer);

        if (RegOpenKeyExA(
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            0,
            KEY_READ,
            &hKey
        ) == ERROR_SUCCESS)
        {
            if (RegQueryValueExA(
                hKey,
                "ProductName",
                nullptr,
                nullptr,
                (LPBYTE)buffer,
                &size
            ) == ERROR_SUCCESS)
            {
                RegCloseKey(hKey);
                return std::string(buffer);
            }

            RegCloseKey(hKey);
        }

        return "Windows";
    }

    std::string GetHostname()
    {
        char buffer[256];
        DWORD size = sizeof(buffer);

        GetComputerNameA(buffer, &size);

        return std::string(buffer);
    }

    std::string GetCPUName()
    {
        HKEY hKey;

        char buffer[256];
        DWORD size = sizeof(buffer);

        const char* path =
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";

        if (RegOpenKeyExA(
            HKEY_LOCAL_MACHINE,
            path,
            0,
            KEY_READ,
            &hKey
        ) == ERROR_SUCCESS)
        {
            if (RegQueryValueExA(
                hKey,
                "ProcessorNameString",
                nullptr,
                nullptr,
                (LPBYTE)buffer,
                &size
            ) == ERROR_SUCCESS)
            {
                RegCloseKey(hKey);
                return std::string(buffer);
            }

            RegCloseKey(hKey);
        }

        return "Unknown CPU";
    }

    uint64_t GetUptimeSeconds()
    {
        return GetTickCount64() / 1000;
    }

    std::string GetArchitecture()
    {
#ifdef _WIN64
        return "x64";
#else
        return "x86";
#endif
    }

    // ------------------------------------------------
    // Virtualization
    // ------------------------------------------------

    VirtualizationInfo GetVirtualizationInfo()
    {
        VirtualizationInfo info;

        int cpuInfo[4] = { 0 };

        __cpuid(cpuInfo, 1);

        info.hypervisorPresent =
            (cpuInfo[2] & (1 << 31)) != 0;

        // Hypervisor vendor
        if (info.hypervisorPresent)
        {
            char vendor[13] = {};

            __cpuid(cpuInfo, 0x40000000);

            memcpy(vendor + 0, &cpuInfo[1], 4);
            memcpy(vendor + 4, &cpuInfo[2], 4);
            memcpy(vendor + 8, &cpuInfo[3], 4);

            info.vendor = vendor;
        }

        // Real VM detection
        HKEY hKey;

        if (RegOpenKeyExA(
            HKEY_LOCAL_MACHINE,
            "HARDWARE\\DESCRIPTION\\System\\BIOS",
            0,
            KEY_READ,
            &hKey
        ) == ERROR_SUCCESS)
        {
            char manufacturer[256] = {};
            char product[256] = {};

            DWORD size1 = sizeof(manufacturer);
            DWORD size2 = sizeof(product);

            RegQueryValueExA(
                hKey,
                "SystemManufacturer",
                nullptr,
                nullptr,
                (LPBYTE)manufacturer,
                &size1
            );

            RegQueryValueExA(
                hKey,
                "SystemProductName",
                nullptr,
                nullptr,
                (LPBYTE)product,
                &size2
            );

            std::string m = manufacturer;
            std::string p = product;

            if (m.find("VMware") != std::string::npos)
            {
                info.runningInVM = true;
                info.vendor = "VMware";
            }

            else if (p.find("VirtualBox") != std::string::npos)
            {
                info.runningInVM = true;
                info.vendor = "VirtualBox";
            }

            else if (
                m.find("Microsoft") != std::string::npos &&
                p.find("Virtual Machine") != std::string::npos
                )
            {
                info.runningInVM = true;
                info.vendor = "Hyper-V";
            }

            else if (
                p.find("KVM") != std::string::npos ||
                p.find("QEMU") != std::string::npos
                )
            {
                info.runningInVM = true;
                info.vendor = "KVM/QEMU";
            }

            RegCloseKey(hKey);
        }

        return info;
    }
}

#endif