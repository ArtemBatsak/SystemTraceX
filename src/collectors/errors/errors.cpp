#include "errors.h"

#include <algorithm>
#include <ctime>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <winevt.h>
#pragma comment(lib, "wevtapi.lib")

namespace SystemErrors {

namespace {
constexpr DWORD kMaxEvents = 120;

Severity MapWindowsLevel(BYTE level)
{
    switch (level) {
    case 1: return Severity::Critical;
    case 2: return Severity::Error;
    case 3: return Severity::Warning;
    default: return Severity::Info;
    }
}

uint64_t FileTimeToUnix(ULONGLONG ft)
{
    if (ft <= 116444736000000000ULL) return 0;
    return (ft - 116444736000000000ULL) / 10000000ULL;
}

std::vector<ErrorEvent> QueryEvents(const wchar_t* channel, const wchar_t* query, const char* defaultSource)
{
    std::vector<ErrorEvent> out;
    EVT_HANDLE hResults = EvtQuery(nullptr, channel, query, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!hResults) return out;

    EVT_HANDLE hEvents[kMaxEvents];
    DWORD count = 0;
    if (EvtNext(hResults, kMaxEvents, hEvents, 0, 0, &count)) {
        for (DWORD i = 0; i < count; ++i) {
            DWORD used = 0, props = 0;
            EvtRender(nullptr, hEvents[i], EvtRenderEventValues, 0, nullptr, &used, &props);
            std::vector<BYTE> buffer(used);

            ErrorEvent ev{};
            ev.source = defaultSource;
            ev.severity = Severity::Error;
            ev.timestamp = static_cast<uint64_t>(std::time(nullptr));
            ev.eventId = 0;

            if (EvtRender(nullptr, hEvents[i], EvtRenderEventValues, used, buffer.data(), &used, &props)) {
                auto* p = reinterpret_cast<PEVT_VARIANT>(buffer.data());

                if (p[0].Type == EvtVarTypeString && p[0].StringVal) {
                    std::wstring ws(p[0].StringVal);
                    ev.source.assign(ws.begin(), ws.end());
                }

                ev.severity = MapWindowsLevel(p[1].ByteVal);

                if (p[7].Type == EvtVarTypeFileTime) {
                    ev.timestamp = FileTimeToUnix(p[7].FileTimeVal);
                }

                ev.eventId = p[8].UInt16Val;
            }

            if (ev.eventId == 1001) {
                ev.message = "Windows Error Reporting: crash/BSOD bucket generated";
            } else if (ev.eventId == 1000) {
                ev.message = "Application crash detected (faulting module/process, see Event Viewer details)";
            } else {
                ev.message = "Serious system/application event detected";
            }

            out.push_back(std::move(ev));
            EvtClose(hEvents[i]);
        }
    }

    EvtClose(hResults);
    return out;
}
} // namespace

ErrorSnapshot GetSnapshot()
{
    ErrorSnapshot snap;

    auto appCrash = QueryEvents(
        L"Application",
        L"*[System[(Level <= 3) and ((EventID=1000) or (EventID=1001)) and (Provider[@Name='Application Error'] or Provider[@Name='Windows Error Reporting'])]]",
        "Application");

    auto bsod = QueryEvents(
        L"System",
        L"*[System[(Level <= 2) and ((EventID=41) or (EventID=1001) or (EventID=6008))]]",
        "System");

    snap.lastEvents.reserve(appCrash.size() + bsod.size());
    snap.lastEvents.insert(snap.lastEvents.end(), appCrash.begin(), appCrash.end());
    snap.lastEvents.insert(snap.lastEvents.end(), bsod.begin(), bsod.end());

    std::sort(snap.lastEvents.begin(), snap.lastEvents.end(), [](const ErrorEvent& a, const ErrorEvent& b) {
        return a.timestamp > b.timestamp;
    });

    if (snap.lastEvents.size() > 80) {
        snap.lastEvents.resize(80);
    }

    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    const uint64_t dayAgo = (now > 86400 ? now - 86400 : 0);
    for (const auto& ev : snap.lastEvents) {
        if (ev.severity == Severity::Critical && ev.timestamp >= dayAgo) {
            snap.criticalEvents.push_back(ev);
        }
    }

    return snap;
}

} // namespace SystemErrors
#endif

#ifdef __linux__
#include <array>
#include <cstdio>
#include <memory>

namespace SystemErrors {
namespace {

void AppendJournal(std::vector<ErrorEvent>& dst, const std::string& command, const std::string& source, Severity severity, const std::string& prefix)
{
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) return;

    char line[1024];
    while (fgets(line, sizeof(line), pipe.get()) != nullptr) {
        std::string msg(line);
        if (!msg.empty() && msg.back() == '\n') msg.pop_back();
        if (msg.empty()) continue;

        ErrorEvent ev{};
        ev.timestamp = static_cast<uint64_t>(std::time(nullptr));
        ev.source = source;
        ev.severity = severity;
        ev.eventId = 0;
        ev.message = prefix + msg;
        dst.push_back(std::move(ev));
    }
}

} // namespace

ErrorSnapshot GetSnapshot()
{
    ErrorSnapshot snap;

    AppendJournal(
        snap.lastEvents,
        "journalctl -b --no-pager -p err..alert -n 80 -o short-iso 2>/dev/null",
        "journalctl",
        Severity::Error,
        "[system] ");

    AppendJournal(
        snap.lastEvents,
        "journalctl -b --no-pager -k -g 'segfault|general protection|BUG:|kernel panic|oops' -n 60 -o short-iso 2>/dev/null",
        "kernel",
        Severity::Critical,
        "[crash-detector] ");

    AppendJournal(
        snap.lastEvents,
        "journalctl -b --no-pager -u systemd-coredump* -n 60 -o short-iso 2>/dev/null",
        "systemd-coredump",
        Severity::Critical,
        "[coredump] ");

    std::sort(snap.lastEvents.begin(), snap.lastEvents.end(), [](const ErrorEvent& a, const ErrorEvent& b) {
        return a.timestamp > b.timestamp;
    });

    if (snap.lastEvents.size() > 120) {
        snap.lastEvents.resize(120);
    }

    for (const auto& ev : snap.lastEvents) {
        if (ev.severity == Severity::Critical) {
            snap.criticalEvents.push_back(ev);
        }
    }

    return snap;
}

} // namespace SystemErrors
#endif
