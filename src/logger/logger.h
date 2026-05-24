#pragma once

#include <filesystem>
#include <mutex>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

/**
 * @brief Класс-обёртка над системой логирования приложения.
 *
 * Отвечает за:
 * 1) Инициализацию spdlog с консольным и файловым sink.
 * 2) Единый путь к файлу логов.
 * 3) Потокобезопасное чтение логов через mutex.
 */
class AppLogger {
public:
    /**
     * @brief Инициализирует глобальный logger (безопасно при повторном вызове).
     */
    static void Init();

    /**
     * @brief Возвращает путь к файлу логов.
     */
    static std::filesystem::path LogFilePath();

    /**
     * @brief Потокобезопасно читает весь лог-файл и возвращает его содержимое.
     */
    static std::string ReadLogs();

private:
    static std::mutex logMutex_;
};

void init_logging();
