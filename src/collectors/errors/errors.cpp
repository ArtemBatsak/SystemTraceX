#include "errors.h"

#ifdef _WIN32
#include <windows.h>
#include <winevt.h>

#pragma comment(lib, "wevtapi.lib")

namespace SystemErrors
{
    static ErrorSnapshot cache;

    void Update()
    {
        // VERY simplified version

        HANDLE h = OpenEventLogA(NULL, "System");

        DWORD read = 0, needed = 0;
        BYTE buffer[65536];

        ReadEventLogA(
            h,
            EVENTLOG_BACKWARDS_READ | EVENTLOG_SEQUENTIAL_READ,
            0,
            buffer,
            sizeof(buffer),
            &read,
            &needed
        );

        CloseEventLog(h);

        ErrorSnapshot snap;

        // parse EVENTLOGRECORD (skipped full parsing for brevity)

        snap.errorCount = 1; // placeholder

        cache = snap;
    }

    ErrorSnapshot GetSnapshot()
    {
        return cache;
    }

    bool Init() { return true; }
}

#endif

#ifdef __linux__

#include <systemd/sd-journal.h>

namespace SystemErrors
{
    static ErrorSnapshot cache;

    void Update()
    {
        sd_journal* j;

        sd_journal_open(&j, SD_JOURNAL_LOCAL_ONLY);

        sd_journal_seek_tail(j);
        sd_journal_previous(j);

        const char* msg;

        ErrorSnapshot snap;

        while (sd_journal_next(j) > 0)
        {
            sd_journal_get_data(j, "MESSAGE", (const void**)&msg, nullptr);

            ErrorEvent e;
            e.message = msg;
            e.source = Source::OS;
            e.severity = Severity::Info;

            snap.lastErrors.push_back(e);

            if (snap.lastErrors.size() > 50)
                break;
        }

        sd_journal_close(j);

        cache = snap;
    }

    ErrorSnapshot GetSnapshot()
    {
        return cache;
    }

    bool Init() { return true; }
}

#endif
