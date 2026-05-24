#include "errors.h"

#include <algorithm>
#include <ctime>
#include <sstream>


#ifdef _WIN32

#include <windows.h>
#include <winevt.h>
#pragma comment(lib, "wevtapi.lib")

namespace SystemErrors {

    bool operator==(const ErrorEvent& a, const ErrorEvent& b)
    {
        return a.eventId == b.eventId && a.timestamp == b.timestamp && a.source == b.source && a.message == b.message;
    }

    bool operator<(const ErrorEvent& a, const ErrorEvent& b)
    {
        if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp; // Sort by timestamp descending
        if (a.eventId != b.eventId) return a.eventId < b.eventId;
        if (a.source != b.source) return a.source < b.source;
        return a.message < b.message;
    }

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

        // Converts wide string to UTF-8
        std::string WideToUtf8(const wchar_t* ws)
        {
            if (!ws || !*ws) return {};
            int size = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
            if (size <= 0) return {};
            std::string out(size - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), size, nullptr, nullptr);
            return out;
        }

        // Sanitizes string for JSON
        std::string SanitizeForJson(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            for (unsigned char c : s) {
                switch (c) {
                case '\\': out += "\\\\"; break;
                case '"':  out += "\\\""; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c >= 0x20) out += c;
                    break;
                }
            }
            return out;
        }

        void DeduplicateAndTrim(std::vector<ErrorEvent>& events, size_t limit)
        {
            if (events.empty()) return;

            // 1. Sort: O(N log N)
            std::sort(events.begin(), events.end());

            // 2. Remove duplicates: O(N)
            auto last = std::unique(events.begin(), events.end());
            events.erase(last, events.end());

            // 3. Limit size: O(1)
            if (events.size() > limit) {
                events.resize(limit);
            }
        }

        std::vector<ErrorEvent> QueryEvents(const wchar_t* channel, const wchar_t* query, const char* defaultSource)
        {
            std::vector<ErrorEvent> out;

            EVT_HANDLE hResults = EvtQuery(nullptr, channel, query, EvtQueryChannelPath | EvtQueryReverseDirection);
            if (!hResults) return out;

            EVT_HANDLE hContext = EvtCreateRenderContext(0, nullptr, EvtRenderContextSystem);
            if (!hContext) {
                EvtClose(hResults);
                return out;
            }

            EVT_HANDLE hEvents[kMaxEvents] = {};
            DWORD count = 0, batchNum = 0;

            while (true) {
                BOOL ok = EvtNext(hResults, kMaxEvents, hEvents, INFINITE, 0, &count);
                DWORD nextErr = GetLastError();
                if (!ok) break;

                for (DWORD i = 0; i < count; ++i) {
                    if (!hEvents[i]) continue;

                    ErrorEvent ev{};
                    ev.source = defaultSource;
                    ev.severity = Severity::Error;
                    ev.timestamp = static_cast<uint64_t>(std::time(nullptr));
                    ev.eventId = 0;

                    // === 1. Render XML from event handle, if available ===
                    std::string xml;
                    {
                        DWORD xmlUsed = 0;
                        EvtRender(nullptr, hEvents[i], EvtRenderEventXml, 0, nullptr, &xmlUsed, nullptr);
                        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && xmlUsed > 0) {
                            std::vector<wchar_t> xmlBuf(xmlUsed / sizeof(wchar_t) + 1);
                            if (EvtRender(nullptr, hEvents[i], EvtRenderEventXml, xmlUsed, xmlBuf.data(), &xmlUsed, nullptr)) {
                                // WideToUtf8 converts wide string to UTF-8
                                xml = WideToUtf8(xmlBuf.data());
                            }
                        }
                    }

                    // === 2. Render event values using hContext ===
                    DWORD used = 0, props = 0;
                    EvtRender(hContext, hEvents[i], EvtRenderEventValues, 0, nullptr, &used, &props);
                    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && used > 0) {
                        std::vector<BYTE> buffer(used);
                        if (EvtRender(hContext, hEvents[i], EvtRenderEventValues, used, buffer.data(), &used, &props)) {
                            auto* values = reinterpret_cast<PEVT_VARIANT>(buffer.data());

                            if (props > EvtSystemProviderName &&
                                values[EvtSystemProviderName].Type == EvtVarTypeString &&
                                values[EvtSystemProviderName].StringVal) {
                                ev.source = WideToUtf8(values[EvtSystemProviderName].StringVal);
                            }

                            if (props > EvtSystemEventID &&
                                values[EvtSystemEventID].Type == EvtVarTypeUInt16)
                                ev.eventId = values[EvtSystemEventID].UInt16Val;

                            if (props > EvtSystemLevel &&
                                values[EvtSystemLevel].Type == EvtVarTypeByte)
                                ev.severity = MapWindowsLevel(values[EvtSystemLevel].ByteVal);

                            if (props > EvtSystemTimeCreated &&
                                values[EvtSystemTimeCreated].Type == EvtVarTypeFileTime) {
                                uint64_t ts = FileTimeToUnix(values[EvtSystemTimeCreated].FileTimeVal);
                                if (ts > 0) ev.timestamp = ts;
                            }
                        }
                    }

                    // === 3. Extract EventData from XML ===
                    auto extractNamed = [&](const std::string& name) -> std::string {
                        for (const auto& q : { std::string("'"), std::string("\"") }) {
                            std::string open = "<Data Name=" + q + name + q + ">";
                            std::string close = "</Data>";
                            auto s = xml.find(open);
                            if (s == std::string::npos) continue;
                            s += open.size();
                            auto e = xml.find(close, s);
                            if (e == std::string::npos) continue;
                            // Sanitize escape characters \ and "
                            return SanitizeForJson(xml.substr(s, e - s));
                        }
                        return {};
                        };

                    if (ev.eventId == 1000) {
                        std::string app = extractNamed("AppName");
                        std::string ver = extractNamed("AppVersion");
                        std::string mod = extractNamed("ModuleName");
                        std::string exCode = extractNamed("ExceptionCode");
                        std::string path = extractNamed("AppPath");
                        ev.message = "App crash: " + (app.empty() ? "unknown" : app);
                        if (!ver.empty())                ev.message += " v" + ver;
                        if (!mod.empty() && mod != app)  ev.message += " | module: " + mod;
                        if (!exCode.empty())             ev.message += " | exception: 0x" + exCode;
                        if (!path.empty())               ev.message += " | path: " + path;
                    }
                    else if (ev.eventId == 1001) {
                        std::string app = extractNamed("AppName");
                        std::string bucket = extractNamed("Bucket");
                        std::string report = extractNamed("ReportId");
                        ev.message = "WER report: " + (app.empty() ? "unknown" : app);
                        if (!bucket.empty()) ev.message += " | bucket: " + bucket;
                        if (!report.empty()) ev.message += " | id: " + report;
                    }
                    else if (ev.eventId == 41) {
                        std::string bugcheck = extractNamed("BugcheckCode");
                        ev.message = "Kernel-Power: unexpected shutdown / BSOD";
                        if (!bugcheck.empty()) ev.message += " | bugcheck: " + bugcheck;
                    }
                    else if (ev.eventId == 6008) {
                        std::string t = extractNamed("param1");
                        std::string d = extractNamed("param2");
                        ev.message = "Unexpected previous shutdown";
                        if (!t.empty() && !d.empty()) ev.message += " at " + d + " " + t;
                    }
                    else {
                        ev.message = "System event ID " + std::to_string(ev.eventId);
                    }

                    out.push_back(std::move(ev));
                    EvtClose(hEvents[i]);
                    hEvents[i] = nullptr;
                }
                count = 0;
                ++batchNum;
            }

            EvtClose(hContext);
            EvtClose(hResults);
            return out;
        }
    } // namespace

    ErrorSnapshot GetSnapshot() {

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
        std::merge(appCrash.begin(), appCrash.end(),
            bsod.begin(), bsod.end(),
            std::back_inserter(snap.lastEvents));


        DeduplicateAndTrim(snap.lastEvents, 80);

        // 2. Оптимизация фильтрации
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        const uint64_t dayAgo = (now > 86400) ? (now - 86400) : 0;

        // Резервируем память для criticalEvents, чтобы избежать реаллокаций
        // Можно взять небольшой запас, чтобы не угадывать точное число
        snap.criticalEvents.reserve((std::min)(snap.lastEvents.size(), size_t(20)));

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
#include <cstring>
#include <iomanip>
#include <memory>

namespace {

    uint64_t ParseShortIsoTimestamp(const std::string& line)
    {
        if (line.size() < 24) return 0;

        std::tm tm{};
        int tzOffsetHours = 0, tzOffsetMins = 0;
        char sign = '+';

        int parsed = sscanf(line.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c%2d%2d",
            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
            &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
            &sign, &tzOffsetHours, &tzOffsetMins);

        if (parsed < 6) return 0;

        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = -1;

        time_t t = timegm(&tm);
        if (t < 0) return 0;

        int offsetSec = (tzOffsetHours * 60 + tzOffsetMins) * 60;
        if (sign == '+') t -= offsetSec;
        else             t += offsetSec;

        return static_cast<uint64_t>(t);
    }

    std::string StripTimestamp(const std::string& line)
    {
        auto pos = line.find(' ');
        if (pos == std::string::npos) return line;
        pos = line.find(' ', pos + 1);
        if (pos == std::string::npos) return line;
        return line.substr(pos + 1);
    }

    void AppendJournal(std::vector<SystemErrors::ErrorEvent>& dst, const std::string& command,
        const std::string& source, SystemErrors::Severity severity, const std::string& prefix)
    {
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
        if (!pipe) return;

        char line[2048];
        while (fgets(line, sizeof(line), pipe.get()) != nullptr) {
            std::string msg(line);
            if (!msg.empty() && msg.back() == '\n') msg.pop_back();
            if (msg.empty()) continue;

            SystemErrors::ErrorEvent ev{};
            ev.timestamp = ParseShortIsoTimestamp(msg);
            if (ev.timestamp == 0)
                ev.timestamp = static_cast<uint64_t>(std::time(nullptr));
            ev.source = source;
            ev.severity = severity;
            ev.eventId = 0;
            ev.message = prefix + StripTimestamp(msg);
            dst.push_back(std::move(ev));
        }
    }

} // namespace

namespace SystemErrors {

    ErrorSnapshot GetSnapshot()
    {
        ErrorSnapshot snap;

        AppendJournal(snap.lastEvents,
            "journalctl -b --no-pager -p err..alert -n 80 -o short-iso 2>/dev/null",
            "journalctl", Severity::Error, "[system] ");

        AppendJournal(snap.lastEvents,
            "journalctl -b --no-pager -k -g 'segfault|general protection|BUG:|kernel panic|oops' -n 60 -o short-iso 2>/dev/null",
            "kernel", Severity::Critical, "[crash-detector] ");

        AppendJournal(snap.lastEvents,
            "journalctl -b --no-pager -u systemd-coredump* -n 60 -o short-iso 2>/dev/null",
            "systemd-coredump", Severity::Critical, "[coredump] ");

        std::sort(snap.lastEvents.begin(), snap.lastEvents.end());

        if (snap.lastEvents.size() > 120)
            snap.lastEvents.resize(120);

        for (const auto& ev : snap.lastEvents) {
            if (ev.severity == Severity::Critical)
                snap.criticalEvents.push_back(ev);
        }

        return snap;
    }

} // namespace SystemErrors
#endif