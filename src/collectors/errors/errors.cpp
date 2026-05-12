#include "errors.h"
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <winevt.h>
#pragma comment(lib, "wevtapi.lib")

namespace SystemErrors {


    std::string GetEventMessage(EVT_HANDLE hEvent) {
        return "System Event (Check Event Viewer for details)";
    }

    ErrorSnapshot GetSnapshot() {
        ErrorSnapshot snap;
		// Read last 50 events with level <= Warning (1-3)
        LPCWSTR query = L"*[System[(Level <= 3)]]";
        EVT_HANDLE hResults = EvtQuery(NULL, L"System", query, EvtQueryChannelPath | EvtQueryReverseDirection);

        if (hResults) {
            EVT_HANDLE hEvents[50];
            DWORD count = 0;

			// if 0 - we don't have more events
            if (EvtNext(hResults, 50, hEvents, 100, 0, &count)) {
                for (DWORD i = 0; i < count; i++) {
                    ErrorEvent ev;
                    DWORD dwBufferUsed = 0;
                    DWORD dwPropertyCount = 0;

					// Take ProviderName, Level, TimeCreated, EventID
                    EvtRender(NULL, hEvents[i], EvtRenderEventValues, 0, NULL, &dwBufferUsed, &dwPropertyCount);
                    std::vector<BYTE> buffer(dwBufferUsed);

                    if (EvtRender(NULL, hEvents[i], EvtRenderEventValues, dwBufferUsed, buffer.data(), &dwBufferUsed, &dwPropertyCount)) {
                        PEVT_VARIANT pValues = (PEVT_VARIANT)buffer.data();

                        // [0] - Provider Name 
                        if (pValues[0].Type == EvtVarTypeString && pValues[0].StringVal) {
                            std::wstring ws(pValues[0].StringVal);
                            ev.source = std::string(ws.begin(), ws.end());
                        }
                        else {
                            ev.source = "Unknown Source";
                        }

                        // [1] - Level 
                        ev.severity = static_cast<Severity>(pValues[1].ByteVal);

                        // [7] - TimeCreated 
                        if (pValues[7].Type == EvtVarTypeFileTime) {
                            ULONGLONG ft = pValues[7].FileTimeVal;
							// convert Windows FILETIME (100-nanosecond intervals since Jan 1, 1601) to Unix time (seconds since Jan 1, 1970)
                            ev.timestamp = (ft - 116444736000000000ULL) / 10000000ULL;
                        }

                        // [8] - Event ID
                        ev.eventId = pValues[8].UInt16Val;

                        ev.message = "System event detected. ID: " + std::to_string(ev.eventId);

                        snap.lastEvents.push_back(ev);
                        if (ev.severity == Severity::Critical) {
                            snap.criticalEvents.push_back(ev);
                        }
                    }
                    EvtClose(hEvents[i]);
                }
            }
            EvtClose(hResults);
        }
        return snap;
    }
}

#endif

#ifdef __linux__
// --- LINUX IMPLEMENTATION ---

#include <cstdio>
#include <memory>
#include <array>
#include <ctime>

namespace SystemErrors {


    ErrorSnapshot GetSnapshot() {
        ErrorSnapshot snap;

		// Take last 10 error events (priority <= 3) from journalctl
        std::string command = "journalctl -n 10 -p 3 --no-pager --output=cat 2>/dev/null";

        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);

        if (!pipe) {
            return snap;
        }

        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
            ErrorEvent ev;
            ev.message = std::string(buffer);
			// Delete trailing newline if exists
            if (!ev.message.empty() && ev.message.back() == '\n') ev.message.pop_back();

            ev.source = "journalctl";
            ev.severity = Severity::Error;
            ev.timestamp = std::time(nullptr);
            ev.eventId = 0;

            snap.lastEvents.push_back(ev);
        }

        return snap;
    }
}
#endif