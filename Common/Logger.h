// Logger.h - Centralized logging interface using spdlog
// Provides file + console logging with level-based filtering.
// Usage:
//   Call InitLogger("logs/capture_server.log") once at startup.
//   Then use LOG_INFO / LOG_WARN / LOG_ERROR / LOG_DEBUG etc. anywhere.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Convenience macros – map to the spdlog default logger
// ---------------------------------------------------------------------------
#define LOG_TRACE(...)    spdlog::trace(__VA_ARGS__)
#define LOG_DEBUG(...)    spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...)     spdlog::info(__VA_ARGS__)
#define LOG_WARN(...)     spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...)    spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)

// ---------------------------------------------------------------------------
// Helper: convert a Windows wchar_t string to UTF-8 std::string
// ---------------------------------------------------------------------------
inline std::string WStrToStr(const wchar_t* wstr)
{
    if (!wstr) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

// ---------------------------------------------------------------------------
// InitLogger
//   logFileName   – path to the rotating log file (directory must be writable)
//   fileLevel     – minimum level written to the log file   (default: debug)
//   consoleLevel  – minimum level written to stdout         (default: info)
//
// Call once at application startup.  Safe to call multiple times (subsequent
// calls are no-ops so that library code cannot accidentally reinitialise the
// logger the host application set up).
// ---------------------------------------------------------------------------
inline void InitLogger(const std::string& logFileName,
                       spdlog::level::level_enum fileLevel    = spdlog::level::debug,
                       spdlog::level::level_enum consoleLevel = spdlog::level::info)
{
    // Guard: only initialise once
    if (spdlog::get("main"))
        return;

    try
    {
        // Console sink (color)
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(consoleLevel);
        consoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

        // Rotating file sink (max 5 MB, keep 3 files)
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFileName, 5 * 1024 * 1024, 3);
        fileSink->set_level(fileLevel);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [tid %t] %v");

        std::vector<spdlog::sink_ptr> sinks{ consoleSink, fileSink };
        auto logger = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace); // logger accepts all; sinks filter
        logger->flush_on(spdlog::level::warn);   // flush immediately on warnings+

        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);
        spdlog::flush_every(std::chrono::seconds(3));
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        // If file logging fails (e.g. read-only filesystem) fall back to console
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        spdlog::warn("Logger init failed ({}); using console only", ex.what());
    }
}
