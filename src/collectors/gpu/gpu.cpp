#include "gpu.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <mutex>
#include <vector>
#include <string>
#include <algorithm>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "pdh.lib")

namespace GPU {

    static bool g_initialized = false;

    // We use a single initialization flag because GPU telemetry is composed of multiple
    // independent subsystems (DXGI adapters, PDH GPU engine counters, and potentially
    // D3D-based memory queries in the future).
    static std::mutex g_mutex;

    static PDH_HQUERY g_query = nullptr;
    static std::vector<PDH_HCOUNTER> g_counters;

    static GpuSnapshot g_snapshot;

    // ------------------------------------------------------------
    // Vendor detect
    // ------------------------------------------------------------

    static Vendor DetectVendor(const std::wstring& name) {

        if (name.find(L"NVIDIA") != std::wstring::npos)
            return Vendor::Nvidia;

        if (name.find(L"AMD") != std::wstring::npos ||
            name.find(L"Radeon") != std::wstring::npos)
            return Vendor::AMD;

        if (name.find(L"Intel") != std::wstring::npos)
            return Vendor::Intel;

        return Vendor::Unknown;
    }

    // ------------------------------------------------------------
    // GPU detection (DXGI)
    // ------------------------------------------------------------

    static void DetectGpus() {

        IDXGIFactory1* factory = nullptr;

        if (FAILED(CreateDXGIFactory1(
            __uuidof(IDXGIFactory1),
            (void**)&factory))) {
            return;
        }

        IDXGIAdapter1* adapter = nullptr;

        std::vector<GpuInfo> result;

        for (UINT i = 0;
            factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
            ++i) {

            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapter->Release();
                continue;
            }

            GpuInfo gpu;

            char name[256] = {};
            wcstombs(name, desc.Description, sizeof(name));

            gpu.name = name;
            gpu.vendor = DetectVendor(desc.Description);

            gpu.vramTotalBytes =
                static_cast<uint64_t>(desc.DedicatedVideoMemory);

            gpu.isIntegrated =
                (gpu.vendor == Vendor::Intel ||
                    gpu.vramTotalBytes < (1ULL << 30));

            gpu.valid = true;

            result.push_back(gpu);

            adapter->Release();
        }

        factory->Release();

        std::lock_guard<std::mutex> lock(g_mutex);

        g_snapshot.gpus = std::move(result);
        g_snapshot.count = static_cast<int>(g_snapshot.gpus.size());
    }

    // ------------------------------------------------------------
    // PDH INIT
    // ------------------------------------------------------------

    static void InitPdh() {

        if (g_query)
            return;

        if (PdhOpenQueryW(nullptr, 0, &g_query) != ERROR_SUCCESS)
            return;

        DWORD size = 0;

        if (PdhExpandWildCardPathW(
            nullptr,
            L"\\GPU Engine(*)\\Utilization Percentage",
            nullptr,
            &size,
            0) != PDH_MORE_DATA)
            return;

        std::vector<wchar_t> buffer(size);

        if (PdhExpandWildCardPathW(
            nullptr,
            L"\\GPU Engine(*)\\Utilization Percentage",
            buffer.data(),
            &size,
            0) != ERROR_SUCCESS)
            return;

        wchar_t* ptr = buffer.data();

        while (*ptr) {

            std::wstring path = ptr;

            if (path.find(L"engtype_3D") != std::wstring::npos ||
                path.find(L"engtype_Compute") != std::wstring::npos ||
                path.find(L"engtype_Copy") != std::wstring::npos ||
                path.find(L"engtype_VideoDecode") != std::wstring::npos) {

                PDH_HCOUNTER counter;

                if (PdhAddEnglishCounterW(
                    g_query,
                    path.c_str(),
                    0,
                    &counter) == ERROR_SUCCESS) {

                    g_counters.push_back(counter);
                }
            }

            ptr += path.size() + 1;
        }

        PdhCollectQueryData(g_query);
    }

    // ------------------------------------------------------------
    // GPU usage
    // ------------------------------------------------------------

    static float GetGpuUsage() {

        if (!g_query)
            return 0.0f;

        PdhCollectQueryData(g_query);

        float maxUsage = 0.0f;

        for (auto c : g_counters) {

            PDH_FMT_COUNTERVALUE v;

            if (PdhGetFormattedCounterValue(
                c,
                PDH_FMT_DOUBLE,
                nullptr,
                &v) == ERROR_SUCCESS) {

                maxUsage = std::max(maxUsage, (float)v.doubleValue);
            }
        }

        return maxUsage;
    }

    // ------------------------------------------------------------
    // API
    // ------------------------------------------------------------

    void Init() {

        if (g_initialized)
            return;

        DetectGpus();
        InitPdh();

        g_initialized = true;
    }

    void Update() {

        if (!g_initialized)
            Init();

        float usage = GetGpuUsage();

        std::lock_guard<std::mutex> lock(g_mutex);

        for (auto& gpu : g_snapshot.gpus) {
            gpu.usagePercent = usage;
        }
    }

    GpuSnapshot GetSnapshots() {

        std::lock_guard<std::mutex> lock(g_mutex);
        return g_snapshot;
    }

} // namespace GPU

#endif

#ifdef __linux__

#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <mutex>
#include <dirent.h>

namespace GPU {

    static std::mutex g_mutex;
    static GpuSnapshot g_snapshot;

    // ------------------------------------------------------------
    // helpers
    // ------------------------------------------------------------

    static std::string ReadFile(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return "";
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static uint64_t ReadUint64(const std::string& path) {
        std::string v = ReadFile(path);
        if (v.empty()) return 0;
        return std::stoull(v);
    }

    // ------------------------------------------------------------
    // vendor detect (simple string match)
    // ------------------------------------------------------------

    static Vendor DetectVendor(const std::string& name) {

        if (name.find("NVIDIA") != std::string::npos)
            return Vendor::Nvidia;

        if (name.find("AMD") != std::string::npos ||
            name.find("Radeon") != std::string::npos)
            return Vendor::AMD;

        if (name.find("Intel") != std::string::npos)
            return Vendor::Intel;

        return Vendor::Unknown;
    }

    // ------------------------------------------------------------
    // read GPU name from sysfs / lspci fallback
    // ------------------------------------------------------------

    static std::string GetGpuName(int cardIndex) {

        std::string path =
            "/sys/class/drm/card" + std::to_string(cardIndex) + "/device/uevent";

        std::ifstream f(path);
        if (f.is_open()) {

            std::string line;
            while (std::getline(f, line)) {
                if (line.find("PCI_ID") != std::string::npos) {
                    return line;
                }
            }
        }

        return "Unknown GPU";
    }

    // ------------------------------------------------------------
    // usage (best effort)
    // ------------------------------------------------------------

    static float GetGpuUsage(int cardIndex) {

        std::string path =
            "/sys/class/drm/card" + std::to_string(cardIndex) +
            "/device/gpu_busy_percent";

        std::string v = ReadFile(path);
        if (v.empty()) return 0.0f;

        return std::stof(v);
    }

    // ------------------------------------------------------------
    // VRAM (AMD/Intel works best)
    // ------------------------------------------------------------

    static void GetVram(int cardIndex,
        uint64_t& total,
        uint64_t& used) {

        std::string base =
            "/sys/class/drm/card" + std::to_string(cardIndex) +
            "/device/";

        uint64_t t = ReadUint64(base + "mem_info_vram_total");
        uint64_t u = ReadUint64(base + "mem_info_vram_used");

        total = t;
        used = u;
    }

    // ------------------------------------------------------------
    // main detection
    // ------------------------------------------------------------

    static void DetectGpus() {

        std::vector<GpuInfo> result;

        for (int i = 0; i < 8; i++) { // usually enough (0-7)

            std::string base =
                "/sys/class/drm/card" + std::to_string(i) + "/device/";

            std::ifstream test(base + "uevent");
            if (!test.is_open())
                continue;

            GpuInfo gpu;

            gpu.name = GetGpuName(i);

            gpu.vendor = DetectVendor(gpu.name);

            GetVram(i, gpu.vramTotalBytes, gpu.vramUsedBytes);

            gpu.usagePercent = GetGpuUsage(i);

            // integrated detection (simple heuristic)
            gpu.isIntegrated =
                (gpu.vendor == Vendor::Intel ||
                    gpu.vramTotalBytes == 0);

            gpu.valid = true;

            result.push_back(gpu);
        }

        std::lock_guard<std::mutex> lock(g_mutex);

        g_snapshot.gpus = std::move(result);
        g_snapshot.count = static_cast<int>(g_snapshot.gpus.size());
    }

    // ------------------------------------------------------------
    // API
    // ------------------------------------------------------------

    void Init() {
        DetectGpus();
    }

    void Update() {
        DetectGpus(); // safe for Linux (light sysfs reads)
    }

    GpuSnapshot GetSnapshots() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_snapshot;
    }

} // namespace GPU

#endif