#include "logger.h"

#include <fstream>
#include <sstream>
#include <vector>

std::mutex AppLogger::logMutex_;

void AppLogger::Init() {
    std::lock_guard<std::mutex> lock(logMutex_);

    if (spdlog::get("log")) {
        return;
    }

    namespace fs = std::filesystem;
    const fs::path logDir = "logs";
    fs::create_directories(logDir);

    constexpr std::size_t maxFileSize = 5 * 1024 * 1024;
    constexpr std::size_t maxFiles = 5;

    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        LogFilePath().string(), maxFileSize, maxFiles);
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    fileSink->set_level(spdlog::level::info);
    consoleSink->set_level(spdlog::level::info);

    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};

    auto logger = std::make_shared<spdlog::logger>("log", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::info);
    logger->set_pattern("[%d.%m.%Y %H:%M:%S.%e] [%l] %v");
    logger->flush_on(spdlog::level::info);

    spdlog::set_default_logger(logger);
    spdlog::info("Logger initialized");
}

std::filesystem::path AppLogger::LogFilePath() {
    return std::filesystem::path("logs") / "log.log";
}

std::string AppLogger::ReadLogs() {
    std::lock_guard<std::mutex> lock(logMutex_);

    std::ifstream file(LogFilePath(), std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void init_logging() {
    AppLogger::Init();
}
