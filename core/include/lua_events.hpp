#pragma once
// Specialisations of LuaEvent<Ev> that expose C++ runtime events to the
// Lua siren.on() event bus. Include this only from runtime.cpp — it pulls in
// lua_host.hpp which transitively includes Lua C headers.

#include <backend/events.hpp>
#include <core.hpp>
#include <lua_host.hpp>
#include <runtime.hpp>
#include <window_state.hpp>

// event::WindowMapped → siren.on("window_map", function(win) ... end)
// win.id, win.class, win.instance, win.workspace (1-indexed)
template<> struct LuaEvent<event::WindowMapped> {
    static constexpr const char* name = "window_map";
    static int push_args(LuaContext& lua, const void* ev_ptr, Core& core) {
        auto* ev = static_cast<const event::WindowMapped*>(ev_ptr);
        lua.new_table();
        lua.push_integer(ev->window);
        lua.set_field(-2, "id");
        auto ws = core.window_state_any(ev->window);
        if (ws) {
            lua.push_string(ws->wm_class);
            lua.set_field(-2, "class");
            lua.push_string(ws->wm_instance);
            lua.set_field(-2, "instance");
            int ws_id = core.workspace_of_window(ev->window);
            lua.push_integer(ws_id >= 0 ? ws_id + 1 : 0);
            lua.set_field(-2, "workspace");
        }
        return 1;
    }
};

// event::WindowUnmapped → siren.on("window_unmap", function(win) ... end)
// win.id, win.withdrawn (bool)
template<> struct LuaEvent<event::WindowUnmapped> {
    static constexpr const char* name = "window_unmap";
    static int push_args(LuaContext& lua, const void* ev_ptr, Core&) {
        auto* ev = static_cast<const event::WindowUnmapped*>(ev_ptr);
        lua.new_table();
        lua.push_integer(ev->window);
        lua.set_field(-2, "id");
        lua.push_bool(ev->withdrawn);
        lua.set_field(-2, "withdrawn");
        return 1;
    }
};

// event::WorkspaceSwitched → siren.on("workspace_switch", function(ev) ... end)
// ev.workspace (1-indexed)
template<> struct LuaEvent<event::WorkspaceSwitched> {
    static constexpr const char* name = "workspace_switch";
    static int push_args(LuaContext& lua, const void* ev_ptr, Core&) {
        auto* ev = static_cast<const event::WorkspaceSwitched*>(ev_ptr);
        lua.new_table();
        lua.push_integer(ev->workspace_id + 1);
        lua.set_field(-2, "workspace");
        return 1;
    }
};

// event::FocusChanged → siren.on("focus_change", function(ev) ... end)
// ev.id (window id, or 0 if focus cleared)
template<> struct LuaEvent<event::FocusChanged> {
    static constexpr const char* name = "focus_change";
    static int push_args(LuaContext& lua, const void* ev_ptr, Core&) {
        auto* ev = static_cast<const event::FocusChanged*>(ev_ptr);
        lua.new_table();
        lua.push_integer(ev->window);
        lua.set_field(-2, "id");
        return 1;
    }
};

// event::ApplyWindowRules → siren.on("window_rules", function(win) ... end)
// win.id, win.class, win.instance, win.workspace (1-indexed), win.type,
// win.from_restart (bool) — skip rules when true to preserve restart state.
// win.type values: "normal", "dialog", "utility", "splash", "modal"
template<> struct LuaEvent<event::ApplyWindowRules> {
    static constexpr const char* name = "window_rules";
    static int push_args(LuaContext& lua, const void* ev_ptr, Core& core) {
        auto* ev = static_cast<const event::ApplyWindowRules*>(ev_ptr);
        lua.new_table();
        lua.push_integer(ev->window);
        lua.set_field(-2, "id");
        lua.push_bool(ev->from_restart);
        lua.set_field(-2, "from_restart");
        auto ws = core.window_state_any(ev->window);
        if (ws) {
            lua.push_string(ws->wm_class);
            lua.set_field(-2, "class");
            lua.push_string(ws->wm_instance);
            lua.set_field(-2, "instance");
            int ws_id = core.workspace_of_window(ev->window);
            lua.push_integer(ws_id >= 0 ? ws_id + 1 : 0);
            lua.set_field(-2, "workspace");
            const char* type_str = "normal";
            switch (ws->type) {
                case WindowType::Dialog:  type_str = "dialog";  break;
                case WindowType::Utility: type_str = "utility"; break;
                case WindowType::Splash:  type_str = "splash";  break;
                case WindowType::Modal:   type_str = "modal";   break;
                default: break;
            }
            lua.push_string(type_str);
            lua.set_field(-2, "type");
        }
        return 1;
    }
};

// event::DisplayTopologyChanged → siren.on("display_change", function() ... end)
template<> struct LuaEvent<event::DisplayTopologyChanged> {
    static constexpr const char* name = "display_change";
    static int push_args(LuaContext&, const void*, Core&) { return 0; }
};

// event::RuntimeStarted → siren.on("start", function() ... end)
// Fired once after Runtime::start() completes — monitors and windows are ready.
template<> struct LuaEvent<event::RuntimeStarted> {
    static constexpr const char* name = "start";
    static int push_args(LuaContext&, const void*, Core&) { return 0; }
};
