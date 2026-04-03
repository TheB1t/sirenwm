#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fmt/printf.h>
#include <memory>
#include <string>
#include <stdexcept>

// Initialize the global spdlog logger.
// Must be called once at startup before any LOG_* macro is used.
// Subsequent calls are no-ops.
inline void log_init(const std::string& log_path = "runtime.log",
    spdlog::level::level_enum level              = spdlog::level::debug) {
    if (spdlog::get("swm"))
        return;

    auto file_sink   = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_path, 1024 * 1024, 2, /*rotate_on_open=*/ true);
    auto stderr_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();

    auto logger      = std::make_shared<spdlog::logger>("swm",
            spdlog::sinks_init_list{ file_sink, stderr_sink });

    logger->set_level(level);
    logger->set_pattern("[%H:%M:%S.%e] [%^%-5l%$] %v");
    logger->flush_on(spdlog::level::err);

    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
}

// Printf-style logging macros — format strings use %s/%d/etc.
// fmt::sprintf handles the formatting so existing call sites need no changes.

#define _LOG_FMT(f_, ...)      ::fmt::sprintf(f_, ##__VA_ARGS__)

#define LOG_DEBUG(format, ...) spdlog::get("swm")->debug(_LOG_FMT(format, ##__VA_ARGS__))
#define LOG_INFO(format, ...)  spdlog::get("swm")->info(_LOG_FMT(format, ##__VA_ARGS__))
#define LOG_WARN(format, ...)  spdlog::get("swm")->warn(_LOG_FMT(format, ##__VA_ARGS__))
#define LOG_ERR(format, ...)   spdlog::get("swm")->error(_LOG_FMT(format, ##__VA_ARGS__))
#define LOG_CRIT(format, ...)  spdlog::get("swm")->critical(_LOG_FMT(format, ##__VA_ARGS__))
#define LOG_FATAL(format, ...) do { \
        spdlog::get("swm")->critical(_LOG_FMT(format, ##__VA_ARGS__)); \
        spdlog::shutdown(); \
        throw std::runtime_error("Fatal error"); \
    } while(0)