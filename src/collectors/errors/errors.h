#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace SystemErrors
{
    enum class Severity {
        Critical = 1,
        Error = 2,
        Warning = 3,
        Info = 4
    };

    struct ErrorEvent {
        uint64_t timestamp;    // Unix time
		std::string source;    // Place of origin (e.g., "OS", "Application")
		Severity severity;     // Level of severity
		std::string message;   // Text of the error message
		uint32_t eventId;      // ID of the event (if available)
    };

    struct ErrorSnapshot {
		std::vector<ErrorEvent> lastEvents;      // 50 last events of any severity
		std::vector<ErrorEvent> criticalEvents;  // Only critical events from the last 24 hours
    };

    ErrorSnapshot GetSnapshot();
}