#include "wallpaper.hpp"

#include <config.hpp>
#include <log.hpp>
#include <module_registry.hpp>

#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// PATH lookup
// ---------------------------------------------------------------------------

static bool find_in_path(const char* name) {
    const char* path_env = getenv("PATH");
    if (!path_env)
        return false;
    std::string            path  = path_env;
    std::string::size_type start = 0;
    while (start < path.size()) {
        auto end = path.find(':', start);
        if (end == std::string::npos)
            end = path.size();
        std::string candidate = path.substr(start, end - start) + "/" + name;
        if (access(candidate.c_str(), X_OK) == 0)
            return true;
        start = end + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// xwallpaper invocation
// ---------------------------------------------------------------------------

static const char* mode_to_flag(const std::string& mode) {
    if (mode == "center")  return "--center";
    if (mode == "zoom")    return "--zoom";
    if (mode == "tile")    return "--tile";
    return "--stretch"; // default / "stretch"
}

void WallpaperModule::apply() {
    if (entries_.empty())
        return;

    if (!find_in_path("xwallpaper")) {
        LOG_ERR("wallpaper: xwallpaper not found in PATH — install it to use this module");
        return;
    }

    const auto& aliases = config().get_monitor_aliases();

    // Build xwallpaper argv: xwallpaper [--output OUT --<mode> IMAGE] ...
    std::vector<std::string> args;
    args.push_back("xwallpaper");

    for (const auto& alias : aliases) {
        if (!alias.enabled)
            continue;
        auto it = entries_.find(alias.alias);
        if (it == entries_.end())
            continue;
        args.push_back("--output");
        args.push_back(alias.output);
        args.push_back(mode_to_flag(it->second.mode));
        args.push_back(it->second.image);
    }

    if (args.size() == 1) {
        // No matching aliases found — nothing to set.
        return;
    }

    std::vector<char*> argv;
    for (auto& s : args)
        argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERR("wallpaper: fork failed: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        execvp("xwallpaper", argv.data());
        _exit(127);
    }
    // Reap child to avoid zombies.
    waitpid(pid, nullptr, 0);
}

// ---------------------------------------------------------------------------
// Lua config: siren.wallpaper = { alias = { image = "...", mode = "..." }, ... }
// ---------------------------------------------------------------------------

static bool parse_wallpaper_table(LuaContext& lua, int table_idx,
    std::unordered_map<std::string, WallpaperEntry>& out,
    std::string& err) {
    if (!lua.is_table(table_idx)) {
        err = "siren.wallpaper must be a table";
        return false;
    }

    out.clear();
    lua.push_nil();
    while (lua.next(table_idx)) {
        // stack: ... key value
        if (!lua.is_string(-2)) {
            lua.pop(1); // pop value, keep key for next()
            continue;
        }
        std::string alias = lua.to_string(-2);

        if (!lua.is_table(-1)) {
            err = "siren.wallpaper['" + alias + "'] must be a table";
            lua.pop(1);
            return false;
        }

        WallpaperEntry entry;

        lua.get_field(-1, "image");
        if (!lua.is_string(-1)) {
            err = "siren.wallpaper['" + alias + "'].image must be a string";
            lua.pop(2);
            return false;
        }
        entry.image = lua.to_string(-1);
        lua.pop(1);

        lua.get_field(-1, "mode");
        if (lua.is_string(-1))
            entry.mode = lua.to_string(-1);
        else
            entry.mode = "stretch";
        lua.pop(1);

        out[alias] = std::move(entry);
        lua.pop(1); // pop value, keep key for next()
    }

    return true;
}

static void register_lua(Config& config,
    std::unordered_map<std::string, WallpaperEntry>& entries) {
    config.register_lua_assignment_handler("wallpaper",
        [&entries](LuaContext& lua, int idx, std::string& err) -> bool {
            return parse_wallpaper_table(lua, idx, entries, err);
        });
}

// ---------------------------------------------------------------------------
// Module lifecycle
// ---------------------------------------------------------------------------

void WallpaperModule::on_init(Core&) {
    register_lua(config(), entries_);
}

void WallpaperModule::on_lua_init(Core&) {
    register_lua(config(), entries_);
}

void WallpaperModule::on_start(Core&) {
    apply();
}

void WallpaperModule::on_reload(Core&) {
    apply();
}

void WallpaperModule::on(Core&, event::DisplayTopologyChanged) {
    apply();
}

SWM_REGISTER_MODULE("wallpaper", WallpaperModule)