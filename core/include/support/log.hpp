#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fmt/printf.h>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <support/strong_id.hpp>

// Promote StrongId/StrongIdCastable to their underlying integer so fmt's
// printf-style path sees a plain arithmetic type. fmt's has_formatter check
// under printf_context rejects any class without an explicit formatter (and
// providing one hits native_formatter pitfalls across fmt 9/11/12), so the
// cleanest fix is to unwrap at the log call boundary.
namespace swm::log_detail {
template <typename Tag, typename U>
constexpr U log_arg(StrongId<Tag, U> id) { return id.get(); }
template <typename Tag, typename U>
constexpr U log_arg(StrongIdCastable<Tag, U> id) { return id.get(); }
template <typename T>
constexpr decltype(auto) log_arg(T&& v) {
    return std::forward<T>(v);
}

template <typename Fmt, typename... Args>
std::string log_sprintf(const Fmt& f, Args&&... args) {
    return ::fmt::sprintf(f, log_arg(std::forward<Args>(args))...);
}
} // namespace swm::log_detail

// Global logger instance — avoids spdlog::get() registry lookup on every call.
// Defined inline so all TUs share the same pointer (C++17 inline variables).
inline std::shared_ptr<spdlog::logger> g_logger;

namespace swm::log_detail {

// Resolve the directory where sirenwm writes its state logs.
// Follows XDG Base Directory Spec: $XDG_STATE_HOME/sirenwm, falling back to
// $HOME/.local/state/sirenwm. Returns empty path if neither env var is set
// (unusual — daemons with no HOME — caller decides what to do).
inline std::filesystem::path resolve_log_dir() {
    namespace fs = std::filesystem;
    if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg)
        return fs::path(xdg) / "sirenwm";
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / ".local" / "state" / "sirenwm";
    return {};
}

} // namespace swm::log_detail

// Initialize the global spdlog logger with a rotating file sink + stderr sink.
// `app_name` becomes the log file stem (e.g. "sirenwm" -> "sirenwm.log",
// rotated to "sirenwm.1.log" … "sirenwm.5.log"). Path resolution follows
// XDG_STATE_HOME; the directory is created on first call.
// Subsequent calls are no-ops.
// 5 MB × 5 files ≈ 25 MB max retained per app, rotate_on_open=false so
// exec-restart appends to the current file rather than nuking history.
inline void log_init(const std::string&        app_name = "sirenwm",
    spdlog::level::level_enum level                     = spdlog::level::debug) {
    if (g_logger)
        return;

    namespace fs = std::filesystem;
    constexpr std::size_t         kMax = 5 * 1024 * 1024; // 5 MiB per file
    constexpr std::size_t         kN   = 5;       // rotated file count

    std::vector<spdlog::sink_ptr> sinks;

    auto                          log_dir = swm::log_detail::resolve_log_dir();
    if (!log_dir.empty()) {
        std::error_code ec;
        fs::create_directories(log_dir, ec);
        if (!ec) {
            auto file = (log_dir / (app_name + ".log")).string();
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                file, kMax, kN, /*rotate_on_open=*/ false));
        }
    }

    sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());

    auto logger = std::make_shared<spdlog::logger>("swm", sinks.begin(), sinks.end());
    logger->set_level(level);
    logger->set_pattern("[%H:%M:%S.%e] [%^%-5l%$] %v");
    // flush_on(trace) fires flush() after every accepted record — nothing is
    // lost if the process is killed between a log call and orderly shutdown.
    logger->flush_on(spdlog::level::trace);

    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    g_logger = logger;
}

// Test-only: initialize a logger that swallows output. Used by unit test
// harnesses so they neither spam stderr nor touch the user's $XDG_STATE_HOME.
inline void log_init_null(spdlog::level::level_enum level = spdlog::level::off) {
    if (g_logger)
        return;
    auto sink   = std::make_shared<spdlog::sinks::null_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("swm", sink);
    logger->set_level(level);
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    g_logger = logger;
}

// Printf-style logging macros — format strings use %s/%d/etc.
// fmt::sprintf handles the formatting so existing call sites need no changes.

#define _LOG_FMT(f_, ...)      ::swm::log_detail::log_sprintf(f_, ##__VA_ARGS__)

// Every LOG_* call flushes immediately — no records lost if the process is
// killed (SIGKILL, oom, crash) between the log call and orderly shutdown.
// The cost is a write()+fsync-free flush per line, which is acceptable here:
// sirenwm is a desktop process, not a high-throughput server.
#define _LOG_EMIT(fn_, format, ...) do { \
        g_logger->fn_(_LOG_FMT(format, ##__VA_ARGS__)); \
        g_logger->flush(); \
    } while (0)

#define LOG_DEBUG(format, ...) _LOG_EMIT(debug, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  _LOG_EMIT(info, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  _LOG_EMIT(warn, format, ##__VA_ARGS__)
#define LOG_ERR(format, ...)   _LOG_EMIT(error, format, ##__VA_ARGS__)
#define LOG_CRIT(format, ...)  _LOG_EMIT(critical, format, ##__VA_ARGS__)
#define LOG_FATAL(format, ...) do { \
        g_logger->critical(_LOG_FMT(format, ##__VA_ARGS__)); \
        spdlog::shutdown(); \
        throw std::runtime_error("Fatal error"); \
    } while(0)
