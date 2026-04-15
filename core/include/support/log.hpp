#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fmt/printf.h>
#include <memory>
#include <string>
#include <stdexcept>

// Global logger instance — avoids spdlog::get() registry lookup on every call.
// Defined inline so all TUs share the same pointer (C++17 inline variables).
inline std::shared_ptr<spdlog::logger> g_logger;

// Initialize the global spdlog logger.
// Must be called once at startup before any LOG_* macro is used.
// Subsequent calls are no-ops.
// Uses append mode (truncate=false) so exec-restart does not wipe the log.
// Flush on every debug message so nothing is lost on crash/kill.
inline void log_init(const std::string& log_path = "runtime.log",
    spdlog::level::level_enum level              = spdlog::level::debug) {
    if (g_logger)
        return;

    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        log_path, /*truncate=*/ false);
    auto stderr_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();

    auto logger = std::make_shared<spdlog::logger>("swm",
            spdlog::sinks_init_list{ file_sink, stderr_sink });

    logger->set_level(level);
    logger->set_pattern("[%H:%M:%S.%e] [%^%-5l%$] %v");
    logger->flush_on(spdlog::level::debug);

    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    g_logger = logger;
}

// Printf-style logging macros — format strings use %s/%d/etc.
// fmt::sprintf handles the formatting so existing call sites need no changes.

#define _LOG_FMT(f_, ...)      ::fmt::sprintf(f_, ##__VA_ARGS__)

#define LOG_DEBUG(format, ...) g_logger->debug(_LOG_FMT(format, ##__VA_ARGS__))
#define LOG_INFO(format, ...)  g_logger->info(_LOG_FMT(format, ##__VA_ARGS__))
#define LOG_WARN(format, ...)  g_logger->warn(_LOG_FMT(format, ##__VA_ARGS__))
#define LOG_ERR(format, ...)   g_logger->error(_LOG_FMT(format, ##__VA_ARGS__))
#define LOG_CRIT(format, ...)  g_logger->critical(_LOG_FMT(format, ##__VA_ARGS__))
#define LOG_FATAL(format, ...) do { \
        g_logger->critical(_LOG_FMT(format, ##__VA_ARGS__)); \
        spdlog::shutdown(); \
        throw std::runtime_error("Fatal error"); \
    } while(0)
