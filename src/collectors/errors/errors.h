#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace SystemErrors
{
    enum class Severity
    {
        Info,
        Warning,
        Error,
        Critical
    };

    enum class Source
    {
        OS,
        Driver,
        Application,
        Hardware,
        Network,
        Disk
    };

    struct ErrorEvent
    {
        uint64_t timestamp = 0;

        Severity severity;
        Source source;

        std::string message;
        std::string processName;
        uint32_t pid = 0;
    };

    struct ErrorSnapshot
    {
        std::vector<ErrorEvent> lastErrors;
        uint32_t errorCount = 0;
        uint32_t warningCount = 0;
        uint32_t criticalCount = 0;
    };

    bool Init();
    void Update();
    ErrorSnapshot GetSnapshot();
}