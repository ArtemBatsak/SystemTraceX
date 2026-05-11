#include "network.h"

#ifdef _WIN32

#include <pdh.h>
#include <pdhmsg.h>
#include <mutex>

#pragma comment(lib, "pdh.lib")

namespace Net
{
    struct WinInterface
    {
        std::wstring name=L"";

        PDH_HCOUNTER rx=0;
        PDH_HCOUNTER tx=0;
    };

    static PDH_HQUERY query;

    static std::vector<WinInterface> interfaces;
    static NetworkSnapshot cache;

    static std::mutex mtx;
    static bool initialized = false;

    bool Init()
    {
        if (initialized) return true;

        if (PdhOpenQuery(NULL, 0, &query) != ERROR_SUCCESS)
            return false;

        // enumerate interfaces (simplified approach)
        // NOTE: real production uses PdhEnumObjectItems

        interfaces.clear();

        // wildcard counters (easiest approach)
        WinInterface i;

        PdhAddEnglishCounterW(
            query,
            L"\\Network Interface(*)\\Bytes Received/sec",
            0,
            &i.rx
        );

        PdhAddEnglishCounterW(
            query,
            L"\\Network Interface(*)\\Bytes Sent/sec",
            0,
            &i.tx
        );

        interfaces.push_back(i);

        PdhCollectQueryData(query);

        initialized = true;
        return true;
    }

    void Update()
    {
        if (!Init()) return;

        PdhCollectQueryData(query);

        PDH_FMT_COUNTERVALUE rxVal;
        PDH_FMT_COUNTERVALUE txVal;

        double totalRx = 0;
        double totalTx = 0;

        // wildcard aggregate approach
        PdhGetFormattedCounterValue(
            interfaces[0].rx,
            PDH_FMT_DOUBLE,
            NULL,
            &rxVal
        );

        PdhGetFormattedCounterValue(
            interfaces[0].tx,
            PDH_FMT_DOUBLE,
            NULL,
            &txVal
        );

        totalRx = rxVal.doubleValue;
        totalTx = txVal.doubleValue;

        NetworkSnapshot snap;
        snap.totalRxPerSec = totalRx;
        snap.totalTxPerSec = totalTx;

        // NOTE: per-interface detail needs enum API (can extend later)

        {
            std::lock_guard<std::mutex> lock(mtx);
            cache = snap;
        }
    }

    NetworkSnapshot GetSnapshot()
    {
        Update();
        std::lock_guard<std::mutex> lock(mtx);
        return cache;
    }

    size_t GetAdapterCount()
    {
        return interfaces.size();
    }
}

#endif
    
#ifdef __linux__

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>

namespace Net
{
    struct LinuxIface
    {
        std::string name;

        uint64_t rxBytes = 0;
        uint64_t txBytes = 0;

        double rxPerSec = 0;
        double txPerSec = 0;
    };

    static std::unordered_map<std::string, LinuxIface> prev;
    static std::unordered_map<std::string, LinuxIface> current;

    static NetworkSnapshot cache;
    static std::mutex mtx;

    static uint64_t lastTime = 0;

    bool Init() { return true; }

    static void Read()
    {
        std::ifstream file("/proc/net/dev");
        std::string line;

        current.clear();

        while (std::getline(file, line))
        {
            if (line.find(':') == std::string::npos)
                continue;

            std::istringstream ss(line);

            std::string iface;
            std::getline(ss, iface, ':');

          
            iface.erase(0, iface.find_first_not_of(" \t"));
            iface.erase(iface.find_last_not_of(" \t") + 1);

            LinuxIface i{};
            i.name = iface;

            // RX bytes
            ss >> i.rxBytes;

            
            uint64_t skip;
            for (int k = 0; k < 7; k++)
                ss >> skip;

            // TX bytes
            ss >> i.txBytes;

            current[iface] = i;
        }
    }

    void Update()
    {
        Read();

        NetworkSnapshot snap;

        for (auto& [name, cur] : current)
        {
            auto& prevIface = prev[name];

            double rx = cur.rxBytes - prevIface.rxBytes;
            double tx = cur.txBytes - prevIface.txBytes;

            LinuxIface out;
            out.name = name;
            out.rxPerSec = rx;
            out.txPerSec = tx;

            InterfaceSnapshot is;
            is.name = name;
            is.rxBytesPerSec = rx;
            is.txBytesPerSec = tx;
            is.rxTotalBytes = cur.rxBytes;
            is.txTotalBytes = cur.txBytes;

            snap.interfaces.push_back(is);

            snap.totalRxPerSec += rx;
            snap.totalTxPerSec += tx;
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            cache = snap;
        }

        prev = current;
    }

    NetworkSnapshot GetSnapshot()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return cache;
    }

    size_t GetAdapterCount()
    {
        return current.size();
    }
}

#endif