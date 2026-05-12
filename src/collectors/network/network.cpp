#include "network.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace Net {

    static double GetCurrentSeconds() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now.time_since_epoch()).count();
    }

    NetworkSnapshot NetworkCollector::GetSnapshot() {
        NetworkSnapshot snap;
        double currentTime = GetCurrentSeconds();

        ULONG outBufLen = 15000;
        std::vector<BYTE> buffer(outBufLen);
        PIP_ADAPTER_ADDRESSES pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

        DWORD dwRetVal = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_ALL_INTERFACES | GAA_FLAG_SKIP_MULTICAST, NULL, pAddresses, &outBufLen);

        if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
            buffer.resize(outBufLen);
            pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
            dwRetVal = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_ALL_INTERFACES | GAA_FLAG_SKIP_MULTICAST, NULL, pAddresses, &outBufLen);
        }

        if (dwRetVal == NO_ERROR) {
            // ИСПРАВЛЕНО: Правильный шаг цикла pCurr = pCurr->Next
            for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr != nullptr; pCurr = pCurr->Next) {
                InterfaceSnapshot iface;

                // 1. Имя для вывода (Кириллица)
                int size_needed = WideCharToMultiByte(CP_UTF8, 0, pCurr->FriendlyName, -1, NULL, 0, NULL, NULL);
                if (size_needed > 0) {
                    iface.name.resize(size_needed - 1);
                    WideCharToMultiByte(CP_UTF8, 0, pCurr->FriendlyName, -1, &iface.name[0], size_needed, NULL, NULL);
                }

				// Unique ID for history tracking (using AdapterName which is stable and unique)
                std::string adapterId = pCurr->AdapterName;

				// 2. Ip address 
                for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurr->FirstUnicastAddress; pUnicast != nullptr; pUnicast = pUnicast->Next) {
                    if (pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
                        char ipStr[INET_ADDRSTRLEN];
                        sockaddr_in* sa_in = reinterpret_cast<sockaddr_in*>(pUnicast->Address.lpSockaddr);
                        inet_ntop(AF_INET, &(sa_in->sin_addr), ipStr, INET_ADDRSTRLEN);
                        iface.ipv4 = ipStr;
                        break;
                    }
                }

                iface.isLoopback = (pCurr->IfType == IF_TYPE_SOFTWARE_LOOPBACK);
                iface.isUp = (pCurr->OperStatus == IfOperStatusUp);

                // 3. Static 
                MIB_IF_ROW2 row;
                ZeroMemory(&row, sizeof(row));
                row.InterfaceLuid = pCurr->Luid;

                if (GetIfEntry2(&row) == NO_ERROR) {
                    iface.rxTotalBytes = row.InOctets;
                    iface.txTotalBytes = row.OutOctets;

                    if (history.count(adapterId)) {
                        double dt = currentTime - history[adapterId].timestamp;
                        if (dt > 0.1) {
							// Use double for diff to prevent overflow issues on high-traffic interfaces
                            double rxDiff = static_cast<double>(iface.rxTotalBytes) - static_cast<double>(history[adapterId].rx);
                            double txDiff = static_cast<double>(iface.txTotalBytes) - static_cast<double>(history[adapterId].tx);

							// Protect against negative values due to counter resets or overflows
                            iface.rxBytesPerSec = (std::max)(0.0, rxDiff / dt);
                            iface.txBytesPerSec = (std::max)(0.0, txDiff / dt);
                        }
                    }
					// Save current totals for next snapshot
                    history[adapterId] = { iface.rxTotalBytes, iface.txTotalBytes, currentTime };
                }

				// 4. Filtering: Only include active interfaces with traffic or loopback
                if (iface.isUp && (iface.rxTotalBytes > 0 || iface.isLoopback)) {
                    if (!iface.isLoopback) {
                        snap.totalRxPerSec += iface.rxBytesPerSec;
                        snap.totalTxPerSec += iface.txBytesPerSec;
                    }
                    snap.interfaces.push_back(iface);
                }
            }
        }
        return snap;
    }

} // namespace Net
#endif

#ifdef __linux__
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

namespace Net {
    static double GetCurrentSeconds() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now.time_since_epoch()).count();
    }
    NetworkSnapshot NetworkCollector::GetSnapshot() {
        NetworkSnapshot snap;
        double currentTime = GetCurrentSeconds();

		// 1. Take IP addresses from getifaddrs
        std::map<std::string, std::string> ipMap;
        struct ifaddrs* ifaddr, * ifa;
        if (getifaddrs(&ifaddr) != -1) {
            for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                    char host[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, host, INET_ADDRSTRLEN);
                    ipMap[ifa->ifa_name] = host;
                }
            }
            freeifaddrs(ifaddr);
        }

		// 2. Take RX/TX bytes from /proc/net/dev
        std::ifstream file("/proc/net/dev");
        std::string line;
        std::getline(file, line); // Skip
        std::getline(file, line); // Skip

        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string name;
            ss >> name;
            name.erase(std::remove(name.begin(), name.end(), ':'), name.end());

            uint64_t rx, tx, dummy;
            ss >> rx; // Receive bytes
            for (int i = 0; i < 7; ++i) ss >> dummy; 
            ss >> tx; // Transmit bytes

            InterfaceSnapshot iface;
            iface.name = name;
            iface.ipv4 = ipMap.count(name) ? ipMap[name] : "0.0.0.0";
            iface.rxTotalBytes = rx;
            iface.txTotalBytes = tx;
            iface.isLoopback = (name == "lo");
			iface.isUp = true; // For simplicity, we assume all interfaces in /proc/net/dev are up. More complex logic can be added if needed.

            if (history.count(name)) {
                double dt = currentTime - history[name].timestamp;
                if (dt > 0.1) {
                    iface.rxBytesPerSec = (rx - history[name].rx) / dt;
                    iface.txBytesPerSec = (tx - history[name].tx) / dt;
                }
            }

            history[name] = { rx, tx, currentTime };

            snap.totalRxPerSec += iface.rxBytesPerSec;
            snap.totalTxPerSec += iface.txBytesPerSec;
            snap.interfaces.push_back(iface);
        }

        return snap;
    }
}
    
#endif

