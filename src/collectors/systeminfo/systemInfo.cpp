#ifdef _WIN32

#include "systemInfo.h"

#include <windows.h>
#include <intrin.h>
#include <string>
namespace SystemInfo {

    // Структура для RtlGetVersion (скрытая часть WinAPI)
    typedef struct _RTL_OSVERSIONINFOEXW {
        ULONG dwOSVersionInfoSize;
        ULONG dwMajorVersion;
        ULONG dwMinorVersion;
        ULONG dwBuildNumber;
        ULONG dwPlatformId;
        WCHAR szCSDVersion[128];
    } RTL_OSVERSIONINFOEXW;

    typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(RTL_OSVERSIONINFOEXW*);

    // Умное определение версии ОС
    static std::string GetPreciseWindowsName() {
        HMODULE hMod = GetModuleHandleA("ntdll.dll");
        if (hMod) {
            auto pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
            if (pRtlGetVersion) {
                RTL_OSVERSIONINFOEXW rovi = { 0 };
                rovi.dwOSVersionInfoSize = sizeof(rovi);
                if (pRtlGetVersion(&rovi) == 0) {
                    DWORD major = rovi.dwMajorVersion;
                    DWORD build = rovi.dwBuildNumber;

                    if (major == 10) {
                        if (build >= 26100) return "Windows 11 (Insider/Next Gen)"; // Пока не 12!
                        if (build >= 22000) return "Windows 11";
                        return "Windows 10";
                    }
                    if (major == 6) {
                        if (rovi.dwMinorVersion == 3) return "Windows 8.1";
                        if (rovi.dwMinorVersion == 2) return "Windows 8";
                        if (rovi.dwMinorVersion == 1) return "Windows 7";
                    }
                }
            }
        }
        return "Windows (Unknown)";
    }

    // Умное определение архитектуры
    static std::string GetSystemArchitecture() {
        SYSTEM_INFO si;
        GetNativeSystemInfo(&si);
        switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
        case PROCESSOR_ARCHITECTURE_ARM64: return "ARM64";
        case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
        default: return "Unknown";
        }
    }

    // Финальная функция снимка
    SystemSnapshot GetSnapshot() {
        SystemSnapshot snap;

        // 1. Имя ПК
        char buf[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD bSize = sizeof(buf);
        if (GetComputerNameA(buf, &bSize)) snap.hostname = buf;

        // 2. ОС и Архитектура
        snap.osName = GetPreciseWindowsName();
        snap.architecture = GetSystemArchitecture();

        // 3. Аптайм
        snap.uptimeSeconds = GetTickCount64() / 1000;

        // 4. Виртуализация
        int cpu[4];
        __cpuid(cpu, 1);
        if ((cpu[2] >> 31) & 1) {
            snap.virtualization.hypervisorPresent = true;
            snap.virtualization.runningInVM = true;

            __cpuid(cpu, 0x40000000);
            char vendor[13];
            memcpy(vendor, &cpu[1], 4);
            memcpy(vendor + 4, &cpu[2], 4);
            memcpy(vendor + 8, &cpu[3], 4);
            vendor[12] = '\0';

            std::string vStr = vendor;
            if (vStr == "Microsoft Hv") {
                snap.virtualization.vendor = "Hyper-V / VBS Mode";
            }
            else {
                snap.virtualization.vendor = vStr;
            }
        }
        else {
            snap.virtualization.vendor = "None (Bare Metal)";
        }

        return snap;
    }
}
#endif

#ifdef __linux__

#include "systemInfo.h"

#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <fstream>

namespace SystemInfo
{


    SystemSnapshot GetSnapshot() {
        SystemSnapshot snap;

		// 1. Data from uname
        struct utsname buffer;
        if (uname(&buffer) == 0) {
            snap.osName = GetLinuxDistroName();   // Linux
			snap.kernelVersion = buffer.release; // For example: 5.15.0-1051-azure
            snap.hostname = buffer.nodename;
            snap.architecture = buffer.machine;  // x86_64
        }

        // 2. Uptime
        struct sysinfo si;
        if (sysinfo(&si) == 0) snap.uptimeSeconds = si.uptime;

        // 3. Virtualization 
        std::ifstream vendorFile("/sys/class/dmi/id/sys_vendor");
        if (vendorFile.is_open()) {
            std::getline(vendorFile, snap.virtualization.vendor);
			// Popular VM vendors
            std::string v = snap.virtualization.vendor;
            if (v == "QEMU" || v == "VMware, Inc." || v == "Microsoft Corporation" || v == "KVM") {
                snap.virtualization.runningInVM = true;
                snap.virtualization.hypervisorPresent = true;
            }
        }

        return snap;
    }

    std::string GetLinuxDistroName() {
        std::ifstream file("/etc/os-release");
        std::string line;
        if (file.is_open()) {
            while (std::getline(file, line)) {
				// Look for the line starting with PRETTY_NAME=
                if (line.compare(0, 12, "PRETTY_NAME=") == 0) {
					std::string name = line.substr(13); // Drop PRETTY_NAME="
					name.pop_back(); // Drop trailing "
                    return name;
                }
            }
        }
        return "Linux (Generic)";
    }
}
#endif
