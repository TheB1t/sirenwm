// This TU implements BarModule::on_init() and BarModule::on_start().
#include "bar_module.hpp"
#include <bar/bar_state.hpp>
#include <backend/backend.hpp>
#include <backend/events.hpp>
#include <core.hpp>
#include <config.hpp>
#include <log.hpp>
#include <runtime.hpp>
#include <algorithm>
#include <unordered_set>
#include <cstdlib>
#include <unistd.h>
#include <sys/timerfd.h>

// Parse a slot value (a table) into a BarSlot.
// Accepts:
//   { __builtin = "tags"|"title"|"tray" }
//   { fn = function [, interval = N] }
static bool parse_slot(LuaContext& lua, Config& config, int val_idx,
    BarSlot& out, const std::string& ctx, std::string* err_out) {
    val_idx = lua.abs_index(val_idx);
    if (!lua.is_table(val_idx)) {
        if (err_out)
            *err_out = ctx + ": expected Widget object or builtin";
        return false;
    }

    lua.get_field(val_idx, "__builtin");
    if (!lua.is_nil(-1)) {
        if (!lua.is_string(-1)) {
            lua.pop();
            if (err_out)
                *err_out = ctx + ".__builtin: must be a string";
            return false;
        }
        std::string name = lua.to_string(-1);
        lua.pop();
        if (name == "tags") {
            out.kind = BarSlotKind::Tags;  return true;
        }
        if (name == "title") {
            out.kind = BarSlotKind::Title; return true;
        }
        if (name == "tray") {
            out.kind = BarSlotKind::Tray;  return true;
        }
        if (err_out)
            *err_out = ctx + ".__builtin: unknown builtin '" + name + "'";
        return false;
    }
    lua.pop();

    lua.get_field(val_idx, "render");
    bool has_render = lua.is_function(-1);
    lua.pop();
    if (!has_render) {
        if (err_out)
            *err_out = ctx + ": expected Widget object with render() method";
        return false;
    }

    auto widget_ref = config.lua().ref_value(val_idx);
    if (!widget_ref.valid()) {
        if (err_out)
            *err_out = ctx + ": failed to ref Widget object";
        return false;
    }

    int interval = 0;
    lua.get_field(val_idx, "interval");
    if (lua.is_integer(-1))
        interval = (int)lua.to_integer(-1);
    lua.pop();
    if (interval < 0)
        interval = 0;

    out.kind     = BarSlotKind::Lua;
    out.widget   = std::move(widget_ref);
    out.interval = interval;
    out.ticks    = interval;  // first refresh_slot() renders immediately
    return true;
}

// Parse a zone list: array of slot tables.
static bool parse_zone(LuaContext& lua, Config& config,
    int table_idx, std::vector<BarSlot>& out,
    const std::string& ctx, std::string* err_out) {
    table_idx = lua.abs_index(table_idx);
    int n = lua.raw_len(table_idx);
    for (int i = 1; i <= n; i++) {
        lua.raw_geti(table_idx, i);
        BarSlot     slot;
        std::string slot_ctx = ctx + "[" + std::to_string(i) + "]";
        if (!parse_slot(lua, config, lua.abs_index(-1), slot, slot_ctx, err_out)) {
            LOG_WARN("bar: skipping %s: %s", slot_ctx.c_str(),
                err_out ? err_out->c_str() : "invalid widget");
            if (err_out) err_out->clear();
            lua.pop();
            continue;
        }
        out.push_back(std::move(slot));
        lua.pop();
    }
    return true;
}

static bool parse_bar_config_from_lua(LuaContext& lua,
    Config& config,
    int table_idx,
    BarPosition pos,
    BarConfig& cfg,
    std::string* err_out = nullptr) {
    table_idx = lua.abs_index(table_idx);
    if (!lua.is_table(table_idx)) {
        if (err_out)
            *err_out = "bar config: expected table";
        return false;
    }
    cfg          = {};
    cfg.position = pos;

    auto read_str = [&](const char* key, std::string& out) {
            lua.get_field(table_idx, key);
            if (lua.is_string(-1))
                out = lua.to_string(-1);
            lua.pop();
        };

    read_str("font",   cfg.font);
    if (cfg.font.empty())
        cfg.font = config.get_theme().font;

    lua.get_field(table_idx, "height");
    if (!lua.is_nil(-1) && !lua.is_integer(-1)) {
        lua.pop();
        if (err_out)
            *err_out = "bar.height: must be an integer";
        return false;
    }
    if (lua.is_integer(-1))
        cfg.height = (int)lua.to_integer(-1);
    lua.pop();

    // Colors: start from theme base colors, then apply per-bar overrides.
    const ThemeConfig& theme = config.get_theme();
    cfg.colors.normal_bg  = theme.alt_bg;
    cfg.colors.normal_fg  = theme.fg;
    cfg.colors.focused_bg = theme.accent;
    cfg.colors.focused_fg = theme.alt_fg;
    cfg.colors.bar_bg     = theme.bg;
    cfg.colors.status_fg  = theme.fg;

    lua.get_field(table_idx, "colors");
    if (!lua.is_nil(-1) && !lua.is_table(-1)) {
        lua.pop();
        if (err_out)
            *err_out = "bar.colors: must be a table";
        return false;
    }
    if (lua.is_table(-1)) {
        int  colors_idx = lua.abs_index(-1);
        auto rc = [&](const char* key, std::string& out) {
                lua.get_field(colors_idx, key);
                if (lua.is_string(-1))
                    out = lua.to_string(-1);
                lua.pop();
            };
        rc("normal_bg",  cfg.colors.normal_bg);
        rc("normal_fg",  cfg.colors.normal_fg);
        rc("focused_bg", cfg.colors.focused_bg);
        rc("focused_fg", cfg.colors.focused_fg);
        rc("bar_bg",     cfg.colors.bar_bg);
        rc("status_fg",  cfg.colors.status_fg);
    }
    lua.pop();

    const char*           zone_keys[] = { "left", "center", "right" };
    std::vector<BarSlot>* zones[]     = { &cfg.left, &cfg.center, &cfg.right };
    const char*           bar_name    = (pos == BarPosition::TOP) ? "bar.top" : "bar.bottom";
    for (int z = 0; z < 3; z++) {
        lua.get_field(table_idx, zone_keys[z]);
        if (!lua.is_nil(-1)) {
            if (!lua.is_table(-1)) {
                lua.pop();
                if (err_out)
                    *err_out = std::string(bar_name) + "." + zone_keys[z] + ": must be a table";
                return false;
            }
            std::string ctx = std::string(bar_name) + "." + zone_keys[z];
            if (!parse_zone(lua, config, lua.abs_index(-1), *zones[z], ctx, err_out)) {
                lua.pop();
                return false;
            }
        }
        lua.pop();
    }

    return true;
}

static bool load_bar_assignment(Config& config, LuaContext& lua, int table_idx, std::string& err) {
    table_idx = lua.abs_index(table_idx);
    if (!lua.is_table(table_idx)) {
        err = "siren.bar: expected table";
        return false;
    }

    lua.get_field(table_idx, "top");
    if (!lua.is_nil(-1)) {
        if (!lua.is_table(-1)) {
            lua.pop();
            err = "siren.bar.top: must be a table";
            return false;
        }
        BarConfig   cfg;
        std::string parse_err;
        if (!parse_bar_config_from_lua(lua, config, lua.abs_index(-1), BarPosition::TOP, cfg, &parse_err)) {
            lua.pop();
            err = parse_err;
            return false;
        }
        config.set_bar_config(std::move(cfg));
    }
    lua.pop();

    lua.get_field(table_idx, "bottom");
    if (!lua.is_nil(-1)) {
        if (!lua.is_table(-1)) {
            lua.pop();
            err = "siren.bar.bottom: must be a table";
            return false;
        }
        BarConfig   cfg;
        std::string parse_err;
        if (!parse_bar_config_from_lua(lua, config, lua.abs_index(-1), BarPosition::BOTTOM, cfg, &parse_err)) {
            lua.pop();
            err = parse_err;
            return false;
        }
        config.set_bottom_bar_config(std::move(cfg));
    }
    lua.pop();

    return true;
}

void BarModule::rebuild_bars() {
    bars.clear();
    bottom_bars.clear();

    // monitor_states() returns coordinates offset by the currently applied inset.
    // Bar windows must be placed at the physical monitor top (before inset),
    // so subtract the current inset to recover the original y origin.
    const auto&          monitors   = core().monitor_states();
    const int            cur_top    = core().monitor_top_inset();
    const int            cur_bottom = core().monitor_bottom_inset();
    std::vector<MonRect> top_rects;
    std::vector<MonRect> bottom_rects;
    top_rects.reserve(monitors.size());
    bottom_rects.reserve(monitors.size());
    for (int i = 0; i < (int)monitors.size(); i++) {
        int phys_y = monitors[i].y() - cur_top;
        int phys_h = monitors[i].height() + cur_top + cur_bottom;
        top_rects.push_back({ i, { monitors[i].x(), phys_y }, monitors[i].size() });
        bottom_rects.push_back({ i, { monitors[i].x(), phys_y + phys_h - bottom_cfg.height },
                                 monitors[i].size() });
    }

    if (top_cfg.height > 0) {
        create_bars(top_cfg.height, top_rects);
        (void)core().dispatch(command::ApplyMonitorTopInset{ top_cfg.height });
    }
    if (bottom_cfg.height > 0) {
        create_bottom_bars(bottom_cfg.height, bottom_rects);
        (void)core().dispatch(command::ApplyMonitorBottomInset{ bottom_cfg.height });
    }
}

bool BarModule::parse_setup(LuaContext& lua, int table_idx, std::string& err) {
    return load_bar_assignment(config(), lua, table_idx, err);
}

void BarModule::on_init() {
    state_provider = [this](int mon_idx) -> BarState {
            BarState    s;
            const auto& monitors = core().monitor_states();
            if (monitors.empty()) return s;

            int mon_ws = (mon_idx >= 0 && mon_idx < (int)monitors.size())
                     ? monitors[mon_idx].active_ws
                     : monitors[core().focused_monitor_index()].active_ws;

            const int safe_mon_idx = (mon_idx >= 0 && mon_idx < (int)monitors.size())
            ? mon_idx : core().focused_monitor_index();

            const auto& ws_ids = core().monitor_workspace_ids(safe_mon_idx);
            for (int ws_id : ws_ids) {
                auto ws = core().workspace_state(ws_id);
                if (!ws)
                    continue;
                bool focused     = (ws_id == mon_ws);
                bool has_windows = !ws->windows.empty();
                bool urgent      = false;
                for (const auto& w : ws->windows)
                    if (w->urgent) { urgent = true; break; }
                s.tags.push_back({ ws_id, ws->name, focused, has_windows, urgent });
            }

            if (mon_ws >= 0) {
                WindowId w = core().ws().last_focused_window(safe_mon_idx, mon_ws);
                if (w == NO_WINDOW && safe_mon_idx == core().focused_monitor_index())
                    if (auto focused = core().focused_window_state())
                        w = focused->id;
                if (w != NO_WINDOW)
                    s.title = backend().window_title(w);
            }
            return s;
        };
}

void BarModule::on_lua_init() {
    auto& lua = config().lua();
    auto  ctx = lua.context();

    // Proxy table: bar.settings = {...} triggers parse_setup immediately.
    ctx.new_table();   // proxy
    ctx.new_table();   // metatable
    lua.push_callback([](LuaContext& lctx, void* ud) -> int {
            // __newindex(proxy, key, value)
            std::string key = lctx.is_string(2) ? lctx.to_string(2) : "";
            if (key == "settings") {
                auto*       mod = static_cast<BarModule*>(ud);
                std::string err;
                    if (!mod->parse_setup(lctx, 3, err))
                    LOG_ERR("bar.settings: %s", err.c_str());
            }
            return 0;
        }, this);
    ctx.set_field(-2, "__newindex");
    ctx.set_metatable(-2);

    lua.set_module_table("bar");
}

void BarModule::on(event::ExposeWindow ev) {
    for (auto& b : bars)
        if (b->id() == ev.window) {
            redraw();
            return;
        }
}

void BarModule::on(event::WindowUnmapped ev) {
    for (auto& slot : trays)
        if (slot.tray && slot.tray->handle_unmap_notify(ev.window))
            break;
    redraw();
}

bool BarModule::on(event::ClientMessageEv ev) {
    // Only the owner tray handles REQUEST_DOCK client messages.
    backend::TrayHost* t = owner_tray();
    if (!t)
        return false;

    // Pre-check: if the icon is already docked in any tray (e.g. after a
    // transfer), ignore the duplicate REQUEST_DOCK that apps send when they
    // see a new MANAGER broadcast on exec-restart.
    if (ev.data[1] == 0) { // SYSTEM_TRAY_REQUEST_DOCK opcode
        WindowId icon_win = ev.data[2];
        for (const auto& slot : trays)
            if (slot.tray && slot.tray->contains_icon(icon_win))
                return true;
    }

    WindowId icon_win = NO_WINDOW;
    bool     handled  = t->handle_client_message(ev, &icon_win);
    if (handled && icon_win != NO_WINDOW) {
        // Do not transfer immediately after docking: the app window may not yet
        // be placed on its final workspace (rules run async), and a premature
        // reparent confuses apps that respond to XEMBED_EMBEDDED_NOTIFY.
        // Rebalancing happens on workspace-switch and window-move events.
        redraw();
    }
    return handled;
}

void BarModule::on(event::DestroyNotify ev) {
    for (auto& slot : trays)
        if (slot.tray && slot.tray->handle_destroy_notify(ev.window))
            break;
    redraw();
}

void BarModule::on(event::ConfigureNotify ev) {
    for (auto& slot : trays)
        if (slot.tray && slot.tray->handle_configure_notify(ev.window))
            break;
    redraw();
}

void BarModule::on(event::PropertyNotify ev) {
    for (auto& slot : trays)
        if (slot.tray && slot.tray->handle_property_notify(ev.window, ev.atom))
            break;
    redraw();
}

void BarModule::on(event::ButtonEv ev) {
    if (ev.release) {
        for (auto& slot : trays)
            if (slot.tray && slot.tray->handle_button_event(ev))
                return;
        return;
    }

    for (auto& slot : trays)
        if (slot.tray && slot.tray->handle_button_event(ev))
            return;

    for (auto& b : bars) {
        if (b->id() != ev.window)
            continue;
        int ws = tag_at(ev.window, ev.event_pos.x());
        if (ws >= 0) {
            (void)core().dispatch(command::SwitchWorkspace{ ws, b->monitor_index() });
            redraw();
        }
        return;
    }
}

void BarModule::on_reload() {
    if (auto& bc = config().get_bar_config(); bc.has_value())
        top_cfg = *bc;
    if (auto& bc = config().get_bottom_bar_config(); bc.has_value())
        bottom_cfg = *bc;

    const ThemeConfig& th = config().get_theme();
    auto apply_theme = [&](BarConfig& cfg) {
        if (cfg.font.empty())              cfg.font              = th.font;
        if (cfg.colors.bar_bg.empty())     cfg.colors.bar_bg     = th.bg;
        if (cfg.colors.normal_fg.empty())  cfg.colors.normal_fg  = th.fg;
        if (cfg.colors.normal_bg.empty())  cfg.colors.normal_bg  = th.alt_bg;
        if (cfg.colors.focused_bg.empty()) cfg.colors.focused_bg = th.accent;
        if (cfg.colors.focused_fg.empty()) cfg.colors.focused_fg = th.alt_fg;
        if (cfg.colors.status_fg.empty())  cfg.colors.status_fg  = th.fg;
    };
    apply_theme(top_cfg);
    apply_theme(bottom_cfg);

    if (top_cfg.height <= 0)
        top_cfg.height = 18;
    // Recreate bar windows and re-apply monitor top inset so tiling
    // geometry matches the bar height after reload.
    rebuild_bars();
    (void)core().dispatch(command::ReconcileNow{});
    rebuild_trays();
    rebalance_tray_icons();
    raise_all();
    redraw();
}

void BarModule::rebuild_trays() {
    if (bars.empty())
        return;

    // Determine owner monitor: prefer the current owner to avoid re-broadcasting
    // MANAGER (which causes all tray clients to re-dock with new icon windows).
    // Only pick a new owner on first run (trays empty) or topology change.
    int owner_mon = -1;
    for (auto& slot : trays) {
        if (slot.tray && slot.tray->owns_selection()) {
            owner_mon = slot.mon_idx;
            break;
        }
    }
    if (owner_mon < 0) {
        // First run: pick focused monitor, fallback to first bar.
        int focused_mon = core().focused_monitor_index();
        for (auto& b : bars) {
            if (b && b->monitor_index() == focused_mon) {
                owner_mon = focused_mon;
                break;
            }
        }
        if (owner_mon < 0 && !bars.empty())
            owner_mon = bars.front()->monitor_index();
    }

    // Build set of monitor indices currently covered by bars.
    std::unordered_set<int> bar_mons;
    for (auto& b : bars)
        if (b) bar_mons.insert(b->monitor_index());

    // Remove slots for monitors that no longer have a bar.
    trays.erase(std::remove_if(trays.begin(), trays.end(),
        [&](const TraySlot& s) {
            return bar_mons.find(s.mon_idx) == bar_mons.end();
        }),
        trays.end());

    for (auto& b : bars) {
        if (!b)
            continue;
        int mon_idx = b->monitor_index();

        // If a tray already exists for this monitor, just reattach geometry.
        backend::TrayHost* existing = tray_for_monitor(mon_idx);
        if (existing) {
            existing->attach_to_bar(b->id(), b->x(), b->y(), b->width());
            continue;
        }

        // New monitor — create a passive tray (owner never changes after first run).
        bool own_selection = (mon_idx == owner_mon) && !owner_tray();
        auto tray          = backend().create_tray_host(
            b->id(), b->x(), b->y(),
            top_cfg.height, own_selection);
        if (!tray || tray->window() == NO_WINDOW) {
            LOG_WARN("Bar: failed to create tray for monitor %d", mon_idx);
            continue;
        }
        LOG_INFO("Bar: tray 0x%x created for monitor %d (owner=%d)",
            tray->window(), mon_idx, own_selection ? 1 : 0);
        TraySlot slot;
        slot.mon_idx = mon_idx;
        slot.tray    = std::move(tray);
        trays.push_back(std::move(slot));
    }
    LOG_INFO("Bar: rebuild_trays done, %d tray(s), owner_mon=%d", (int)trays.size(), owner_mon);
}

void BarModule::on(event::TrayIconDocked ev) {
    // Icon returned to root (ReparentNotify to root) — re-adopt into owner tray.
    backend::TrayHost* t = owner_tray();
    if (t && !t->contains_icon(ev.icon))
        t->adopt_icon(ev.icon);
    // Now move it to the correct monitor.
    int target = monitor_for_icon(ev.icon);
    route_icon_to_monitor(ev.icon, target);
    redraw();
}

void BarModule::on(event::DisplayTopologyChanged) {
    rebuild_bars();
    (void)core().dispatch(command::ReconcileNow{});
    rebuild_trays();
    rebalance_tray_icons();
    raise_all();
    redraw();
}

void BarModule::on_start() {
    render_port_ = backend().render_port();
    if (!render_port_) {
        LOG_ERR("Bar: backend render port is unavailable");
        return;
    }
    if (auto& bc = config().get_bar_config(); bc.has_value())
        top_cfg = *bc;
    if (auto& bc = config().get_bottom_bar_config(); bc.has_value())
        bottom_cfg = *bc;

    // Theme is parsed after bar.settings in post-exec — patch colors/font now.
    const ThemeConfig& th = config().get_theme();
    auto apply_theme = [&](BarConfig& cfg) {
        if (cfg.font.empty())
            cfg.font = th.font;
        if (cfg.colors.bar_bg.empty())    cfg.colors.bar_bg     = th.bg;
        if (cfg.colors.normal_fg.empty()) cfg.colors.normal_fg  = th.fg;
        if (cfg.colors.normal_bg.empty()) cfg.colors.normal_bg  = th.alt_bg;
        if (cfg.colors.focused_bg.empty()) cfg.colors.focused_bg = th.accent;
        if (cfg.colors.focused_fg.empty()) cfg.colors.focused_fg = th.alt_fg;
        if (cfg.colors.status_fg.empty()) cfg.colors.status_fg  = th.fg;
    };
    apply_theme(top_cfg);
    apply_theme(bottom_cfg);

    if (top_cfg.height <= 0 && !config().get_bar_config().has_value())
        top_cfg.height = 0;
    else if (top_cfg.height <= 0)
        top_cfg.height = 18;

    if (bottom_cfg.height <= 0 && config().get_bottom_bar_config().has_value())
        bottom_cfg.height = 18;

    rebuild_bars();
    (void)core().dispatch(command::ReconcileNow{});

    rebuild_trays();

    if (pipe(wakeup_pipe) == 0) {
        fcntl(wakeup_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(wakeup_pipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(wakeup_pipe[1], F_SETFD, FD_CLOEXEC);
        runtime().watch_fd(wakeup_pipe[0], [this]() {
                char buf[64];
                while (read(wakeup_pipe[0], buf, sizeof(buf)) > 0) {
                }
                redraw();
            });
    }

    // Collect all Lua slots across both bar configs for timer and init.
    auto lua_slots = [](BarConfig& cfg) -> std::vector<BarSlot*> {
            std::vector<BarSlot*> out;
            for (auto& s : cfg.left)   if (s.kind == BarSlotKind::Lua) out.push_back(&s);
            for (auto& s : cfg.center) if (s.kind == BarSlotKind::Lua) out.push_back(&s);
            for (auto& s : cfg.right)  if (s.kind == BarSlotKind::Lua) out.push_back(&s);
            return out;
        };
    auto top_lua    = lua_slots(top_cfg);
    auto bottom_lua = lua_slots(bottom_cfg);
    bool has_lua    = !top_lua.empty() || !bottom_lua.empty();

    // If any Lua widgets exist, set up a 1-second timerfd for refresh.
    if (has_lua) {
        timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (timer_fd >= 0) {
            struct itimerspec ts = {};
            ts.it_value.tv_sec    = 1;
            ts.it_interval.tv_sec = 1;
            timerfd_settime(timer_fd, 0, &ts, nullptr);
            runtime().watch_fd(timer_fd, [this]() {
                    uint64_t expirations;
                    if (read(timer_fd, &expirations, sizeof(expirations)) > 0) {
                        refresh_widgets();
                        redraw();
                    }
                });
            LOG_INFO("Bar: widget timer started (1s base tick)");
        }
    }

    redraw();

    LOG_INFO("Bar: initialized, height=%d (Cairo/Pango)", top_cfg.height);
}
