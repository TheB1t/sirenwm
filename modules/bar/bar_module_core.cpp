// This TU implements BarModule::on_init() and BarModule::on_start().
#include "bar_module.hpp"
#include <bar/bar_state.hpp>
#include <backend/backend.hpp>
#include <backend/events.hpp>
#include <core.hpp>
#include <log.hpp>
#include <protocol/keyboard.hpp>
#include <protocol/system_tray.hpp>
#include <runtime.hpp>
#include <surface.hpp>
#include <algorithm>
#include <unordered_set>
#include <cstdlib>
#include <unistd.h>
#include <sys/timerfd.h>

// Parse a BarSlot from a Lua table: either a builtin widget ("tags", "title", "tray")
// or a Lua object with a render() method and an optional update interval in seconds.
static bool parse_slot(LuaContext& lua, LuaHost& host, int val_idx,
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

    auto widget_ref = host.ref_value(val_idx);
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

// Parse an array of slot tables into a zone (left/center/right). Skips invalid entries.
static bool parse_zone(LuaContext& lua, LuaHost& host,
    int table_idx, std::vector<BarSlot>& out,
    const std::string& ctx, std::string* err_out) {
    table_idx = lua.abs_index(table_idx);
    int n = lua.raw_len(table_idx);
    for (int i = 1; i <= n; i++) {
        lua.raw_geti(table_idx, i);
        BarSlot     slot;
        std::string slot_ctx = ctx + "[" + std::to_string(i) + "]";
        if (!parse_slot(lua, host, lua.abs_index(-1), slot, slot_ctx, err_out)) {
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
    LuaHost& host,
    const ThemeConfig& theme,
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
        cfg.font = theme.font;

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

    // Color resolution: theme defaults first, then bar-level overrides on top.
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
            if (!parse_zone(lua, host, lua.abs_index(-1), *zones[z], ctx, err_out)) {
                lua.pop();
                return false;
            }
        }
        lua.pop();
    }

    return true;
}

// Parse one bar side from a Lua table with three-state cascade semantics:
//   absent key  → Remove  (no bar on this side)
//   key = {}    → Inherit (use the default config)
//   key = {...} → Custom  (use the provided config)
static bool parse_bar_side(LuaContext& lua, LuaHost& host,
    const ThemeConfig& theme, int table_idx, const char* key,
    BarPosition pos, BarSide& out,
    const std::string& ctx, std::string* err_out) {
    lua.get_field(table_idx, key);
    if (lua.is_nil(-1)) {
        lua.pop();
        out = { BarSideState::Remove, {} };
        return true;
    }
    if (!lua.is_table(-1)) {
        lua.pop();
        if (err_out) *err_out = ctx + "." + key + ": must be a table";
        return false;
    }
    // Truly empty table (no array or string keys) → Inherit.
    if (lua.raw_len(lua.abs_index(-1)) == 0) {
        lua.push_nil();
        bool has_keys = lua.next(lua.abs_index(-2));
        if (has_keys) lua.pop(2);
        if (!has_keys) {
            lua.pop();
            out = { BarSideState::Inherit, {} };
            return true;
        }
    }
    BarConfig   cfg;
    std::string perr;
    if (!parse_bar_config_from_lua(lua, host, theme, lua.abs_index(-1), pos, cfg, &perr)) {
        lua.pop();
        if (err_out) *err_out = perr;
        return false;
    }
    lua.pop();
    out = { BarSideState::Custom, std::move(cfg) };
    return true;
}

// Parse top and bottom sides from a Lua table into a MonitorBarConfig.
static bool parse_monitor_bar_config(LuaContext& lua, LuaHost& host,
    const ThemeConfig& theme, int table_idx,
    MonitorBarConfig& out, const std::string& ctx, std::string* err_out) {
    table_idx = lua.abs_index(table_idx);
    if (!parse_bar_side(lua, host, theme, table_idx, "top",
        BarPosition::TOP,    out.top,    ctx, err_out))
        return false;
    if (!parse_bar_side(lua, host, theme, table_idx, "bottom",
        BarPosition::BOTTOM, out.bottom, ctx, err_out))
        return false;
    return true;
}

// Returns true if the settings table uses per-monitor format (has a "default" key
// or any key that is not "top"/"bottom"), false if it is the legacy flat format.
static bool is_per_monitor_format(LuaContext& lua, int table_idx) {
    table_idx = lua.abs_index(table_idx);
    lua.get_field(table_idx, "default");
    bool has_default = !lua.is_nil(-1);
    lua.pop();
    if (has_default)
        return true;
    lua.push_nil();
    while (lua.next(table_idx)) {
        lua.pop(1);
        if (lua.is_string(-1)) {
            std::string k = lua.to_string(-1);
            if (k != "top" && k != "bottom") {
                lua.pop(1);
                return true;
            }
        }
    }
    return false;
}

static bool load_bar_set_config(LuaHost& host, const ThemeConfig& theme,
    TypedSetting<BarSetConfig>& setting,
    LuaContext& lua, int table_idx, std::string& err) {
    table_idx = lua.abs_index(table_idx);
    if (!lua.is_table(table_idx)) {
        err = "bar.settings: expected table";
        return false;
    }

    BarSetConfig cfg;

    if (is_per_monitor_format(lua, table_idx)) {
        lua.get_field(table_idx, "default");
        if (!lua.is_nil(-1)) {
            if (!lua.is_table(-1)) {
                lua.pop();
                err = "bar.settings.default: must be a table";
                return false;
            }
            if (!parse_monitor_bar_config(lua, host, theme, lua.abs_index(-1),
                cfg.default_cfg, "bar.settings.default", &err)) {
                lua.pop();
                return false;
            }
        }
        lua.pop();

        lua.push_nil();
        while (lua.next(table_idx)) {
            if (!lua.is_string(-2)) {
                lua.pop(1); continue;
            }
            std::string key = lua.to_string(-2);
            if (key == "default") {
                lua.pop(1); continue;
            }

            if (!lua.is_table(-1)) {
                LOG_WARN("bar.settings.%s: expected table, skipping", key.c_str());
                lua.pop(1);
                continue;
            }
            MonitorBarConfig mcfg;
            std::string      ctx = "bar.settings." + key;
            std::string      perr;
            if (!parse_monitor_bar_config(lua, host, theme, lua.abs_index(-1),
                mcfg, ctx, &perr)) {
                LOG_WARN("%s: %s — skipping", ctx.c_str(), perr.c_str());
                lua.pop(1);
                continue;
            }
            cfg.per_monitor[key] = std::move(mcfg);
            lua.pop(1);
        }
    } else {
        // Legacy flat format { top = {...}, bottom = {...} } — treat as the default config.
        if (!parse_monitor_bar_config(lua, host, theme, table_idx,
            cfg.default_cfg, "bar.settings", &err))
            return false;
    }

    setting.set(std::move(cfg));
    return true;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string BarModule::monitor_alias(int mon_idx) const {
    const auto& aliases  = core.current_settings().monitor_aliases;
    const auto& monitors = core.monitor_states();
    if (mon_idx < 0 || mon_idx >= (int)monitors.size())
        return {};
    const std::string& mon_name = monitors[mon_idx].name;
    for (const auto& a : aliases)
        if (a.output == mon_name)
            return a.alias;
    return mon_name; // fallback: use output name directly
}

std::vector<Surface*> BarModule::top_bar_surfaces() const {
    std::vector<Surface*> out;
    for (const auto& b : all_bars_)
        if (b.is_top) out.push_back(b.surface.get());
    return out;
}

std::vector<Surface*> BarModule::bottom_bar_surfaces() const {
    std::vector<Surface*> out;
    for (const auto& b : all_bars_)
        if (!b.is_top) out.push_back(b.surface.get());
    return out;
}

void BarModule::create_bar_window(const MonRect& m, const BarConfig& cfg, bool is_top) {
    SurfaceCreateInfo info;
    info.monitor_index       = m.idx;
    info.pos                 = m.pos;
    info.size                = { m.size.x(), cfg.height };
    info.want_expose         = true;
    info.want_button_press   = true;
    info.want_button_release = true;
    info.dock                = true;
    info.keep_above          = true;

    auto s = runtime.create_surface(info);
    if (!s) return;

    BarWindow bw;
    bw.cfg     = cfg;
    bw.is_top  = is_top;
    bw.surface = std::move(s);
    all_bars_.push_back(std::move(bw));
}

void BarModule::rebuild_bars() {
    all_bars_.clear();

    const auto& monitors = core.monitor_states();

    auto        has_content = [](const BarConfig& cfg) {
            return cfg.height > 0 || !cfg.left.empty()
                   || !cfg.center.empty() || !cfg.right.empty();
        };

    for (int i = 0; i < (int)monitors.size(); i++) {
        std::string      alias = monitor_alias(i);
        MonitorBarConfig mcfg  = bar_set_cfg_.resolve(alias);

        // Physical rect of this monitor: undo any currently applied insets so
        // new bars position correctly even while old ones are still in effect.
        auto [phy_pos, phy_size] = monitors[i].physical();
        MonRect m{ i, phy_pos, phy_size, alias };

        int     top_h    = 0;
        int     bottom_h = 0;

        if (mcfg.top.state == BarSideState::Custom) {
            BarConfig top_cfg = mcfg.top.cfg;
            if (top_cfg.height <= 0) top_cfg.height = kBarDefaultHeight;
            if (has_content(top_cfg)) {
                create_bar_window(m, top_cfg, true);
                top_h = top_cfg.height;
            }
        }

        if (mcfg.bottom.state == BarSideState::Custom) {
            BarConfig bottom_cfg = mcfg.bottom.cfg;
            if (bottom_cfg.height <= 0) bottom_cfg.height = kBarDefaultHeight;
            if (has_content(bottom_cfg)) {
                MonRect mb{ i, { phy_pos.x(), phy_pos.y() + phy_size.y() - bottom_cfg.height },
                            phy_size, alias };
                create_bar_window(mb, bottom_cfg, false);
                bottom_h = bottom_cfg.height;
            }
        }

        (void)core.dispatch(command::atom::ReserveMonitorArea{ i, MonitorEdge::Top, top_h });
        (void)core.dispatch(command::atom::ReserveMonitorArea{ i, MonitorEdge::Bottom, bottom_h });
    }
}

bool BarModule::parse_setup(LuaContext& lua, int table_idx, std::string& err) {
    const ThemeConfig& theme = core.current_settings().theme;
    return load_bar_set_config(this->lua, theme, bar_set_setting_, lua, table_idx, err);
}

void BarModule::on_init() {
    store.register_setting("bar_set", bar_set_setting_);

    state_provider = [this](int mon_idx) -> BarState {
            BarState    s;
            const auto& monitors = core.monitor_states();
            if (monitors.empty()) return s;

            int mon_ws = (mon_idx >= 0 && mon_idx < (int)monitors.size())
                     ? monitors[mon_idx].active_ws
                     : monitors[core.focused_monitor_index()].active_ws;

            const int safe_mon_idx = (mon_idx >= 0 && mon_idx < (int)monitors.size())
            ? mon_idx : core.focused_monitor_index();

            const auto& ws_ids = core.monitor_workspace_ids(safe_mon_idx);
            for (int ws_id : ws_ids) {
                auto ws = core.workspace_state(ws_id);
                if (!ws)
                    continue;
                bool focused     = (ws_id == mon_ws);
                bool has_windows = !ws->windows.empty();
                bool urgent      = false;
                for (const auto& w : ws->windows)
                    if (w->urgent) {
                        urgent = true; break;
                    }
                s.tags.push_back({ ws_id, ws->name, focused, has_windows, urgent });
            }

            if (mon_ws >= 0) {
                WindowId w = core.ws().last_focused_window(safe_mon_idx, mon_ws);
                if (w == NO_WINDOW && safe_mon_idx == core.focused_monitor_index())
                    if (auto focused = core.focused_window_state())
                        w = focused->id;
                if (w != NO_WINDOW) {
                    if (auto ws = core.window_state_any(w))
                        s.title = ws->title;
                }
            }
            return s;
        };
}

void BarModule::on_lua_init() {
    auto& host = this->lua;
    auto  ctx  = host.context();

    // Proxy table: bar.settings = {...} triggers parse_setup immediately.
    ctx.new_table();   // proxy
    ctx.new_table();   // metatable
    host.push_callback([](LuaContext& lctx, void* ud) -> int {
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

    host.set_module_table("bar");
}

static void apply_theme_to_cfg(BarConfig& cfg, const ThemeConfig& th) {
    if (cfg.font.empty())              cfg.font = th.font;
    if (cfg.colors.bar_bg.empty())     cfg.colors.bar_bg = th.bg;
    if (cfg.colors.normal_fg.empty())  cfg.colors.normal_fg = th.fg;
    if (cfg.colors.normal_bg.empty())  cfg.colors.normal_bg = th.alt_bg;
    if (cfg.colors.focused_bg.empty()) cfg.colors.focused_bg = th.accent;
    if (cfg.colors.focused_fg.empty()) cfg.colors.focused_fg = th.alt_fg;
    if (cfg.colors.status_fg.empty())  cfg.colors.status_fg = th.fg;
}

static void apply_theme_to_monitor_cfg(MonitorBarConfig& mcfg, const ThemeConfig& th) {
    if (mcfg.top.state    == BarSideState::Custom) apply_theme_to_cfg(mcfg.top.cfg,    th);
    if (mcfg.bottom.state == BarSideState::Custom) apply_theme_to_cfg(mcfg.bottom.cfg, th);
}

void BarModule::on(event::ExposeSurface ev) {
    for (auto& b : all_bars_)
        if (b.surface.get() == ev.surface) {
            redraw();
            return;
        }
}

void BarModule::on(event::WindowUnmapped ev) {
    for (auto& b : all_bars_)
        if (b.tray && b.tray->handle_unmap_notify(ev.window))
            break;
    redraw();
}

bool BarModule::on(event::ClientMessageEv ev) {
    backend::TrayHost* t = owner_tray();
    if (!t)
        return false;

    if (ev.data[1] == 0) {
        WindowId icon_win = ev.data[2];
        for (const auto& b : all_bars_)
            if (b.tray && b.tray->contains_icon(icon_win))
                return true;
    }

    WindowId icon_win = NO_WINDOW;
    bool     handled  = t->handle_client_message(ev, &icon_win);
    if (handled && icon_win != NO_WINDOW)
        redraw();
    return handled;
}

void BarModule::on(event::DestroyNotify ev) {
    for (auto& b : all_bars_)
        if (b.tray && b.tray->handle_destroy_notify(ev.window))
            break;
    redraw();
}

void BarModule::on(event::ConfigureNotify ev) {
    for (auto& b : all_bars_)
        if (b.tray && b.tray->handle_configure_notify(ev.window))
            break;
    redraw();
}

void BarModule::on(event::PropertyNotify ev) {
    for (auto& b : all_bars_)
        if (b.tray && b.tray->handle_property_notify(ev.window, ev.atom))
            break;
    redraw();
}

void BarModule::on(event::ButtonEv ev) {
    // Surface button events are re-emitted by Runtime as SurfaceButton — this
    // handler only sees events targeting tray icon windows (or other unrelated
    // windows, which we ignore).
    for (auto& b : all_bars_)
        if (b.tray && b.tray->handle_button_event(ev))
            return;
}

void BarModule::on(event::SurfaceButton ev) {
    for (auto& b : all_bars_) {
        if (!b.is_top || b.surface.get() != ev.surface)
            continue;
        if (ev.release)
            return;
        int ws = tag_at(b.surface.get(), ev.event_pos.x());
        if (ws >= 0) {
            (void)core.dispatch(command::atom::SwitchWorkspace{ ws, b.surface->monitor_index() });
            redraw();
        }
        return;
    }
}

void BarModule::on_reload() {
    bar_set_cfg_ = bar_set_setting_.get();

    const ThemeConfig& th = core.current_settings().theme;
    apply_theme_to_monitor_cfg(bar_set_cfg_.default_cfg, th);
    for (auto& [alias, mcfg] : bar_set_cfg_.per_monitor)
        apply_theme_to_monitor_cfg(mcfg, th);

    rebuild_bars();
    (void)core.dispatch(command::atom::ReconcileNow{});
    rebuild_trays();
    rebalance_tray_icons();
    raise_all();
    redraw();
}

void BarModule::rebuild_trays() {
    // Tray ownership is rebuilt fresh — rebuild_bars() destroyed all previous
    // surfaces (and therefore all previous trays attached to them). Choose one
    // top bar to own the _NET_SYSTEM_TRAY_S selection: prefer focused monitor,
    // fall back to the first top bar.
    int focused_mon = core.focused_monitor_index();
    int owner_mon   = -1;
    for (auto& b : all_bars_) {
        if (!b.is_top || !b.surface) continue;
        if (b.surface->monitor_index() == focused_mon) {
            owner_mon = focused_mon;
            break;
        }
    }
    if (owner_mon < 0) {
        for (auto& b : all_bars_)
            if (b.is_top && b.surface) {
                owner_mon = b.surface->monitor_index();
                break;
            }
    }

    int created = 0;
    for (auto& b : all_bars_) {
        if (!b.is_top || !b.surface) continue;
        int  mon_idx       = b.surface->monitor_index();
        bool own_selection = (mon_idx == owner_mon);

        auto tray = runtime.create_tray(*b.surface, own_selection);
        if (!tray || tray->window() == NO_WINDOW) {
            LOG_WARN("Bar: failed to create tray for monitor %d", mon_idx);
            continue;
        }
        LOG_INFO("Bar: tray 0x%x created for monitor %d (owner=%d)",
            tray->window(), mon_idx, own_selection ? 1 : 0);
        b.tray = std::move(tray);
        created++;
    }
    LOG_INFO("Bar: rebuild_trays done, %d tray(s), owner_mon=%d", created, owner_mon);
}

void BarModule::on(const event::CustomEvent& ev) {
    if (auto* docked = ev.msg.unpack<protocol::system_tray::IconDocked>()) {
        backend::TrayHost* t = owner_tray();
        if (t && !t->contains_icon(docked->icon))
            t->adopt_icon(docked->icon);
        int target = monitor_for_icon(docked->icon);
        route_icon_to_monitor(docked->icon, target);
        redraw();
        return;
    }
    if (ev.msg.unpack<protocol::keyboard::LayoutChanged>()) {
        redraw();
        return;
    }
}

void BarModule::on(event::DisplayTopologyChanged) {
    rebuild_bars();
    (void)core.dispatch(command::atom::ReconcileNow{});
    rebuild_trays();
    rebalance_tray_icons();
    raise_all();
    redraw();
}

void BarModule::on_start() {
    bar_set_cfg_ = bar_set_setting_.get();

    const ThemeConfig& th = core.current_settings().theme;
    apply_theme_to_monitor_cfg(bar_set_cfg_.default_cfg, th);
    for (auto& [alias, mcfg] : bar_set_cfg_.per_monitor)
        apply_theme_to_monitor_cfg(mcfg, th);

    rebuild_bars();
    (void)core.dispatch(command::atom::ReconcileNow{});

    rebuild_trays();

    int pipe_fds[2];
    if (pipe(pipe_fds) == 0) {
        if (fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK) < 0 ||
            fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC) < 0) {
            LOG_ERR("bar: fcntl() failed on wakeup pipe");
            close(pipe_fds[0]);
            close(pipe_fds[1]);
        } else {
            int rd = pipe_fds[0];
            wakeup_pipe_wr_ = pipe_fds[1];
            wakeup_pipe_rd_ = EventLoop::FdHandle(runtime.event_loop, rd, [this, rd]() {
                    // Drain the pipe (O_NONBLOCK: stops at EAGAIN/EWOULDBLOCK).
                    char buf[64];
                    ssize_t n;
                    do {
                        n = read(rd, buf, sizeof(buf));
                    } while (n > 0 || (n < 0 && errno == EINTR));
                    redraw();
                });
        }
    }

    // Collect all Lua slots across all bar configs for timer setup.
    bool has_lua = false;
    for (const auto& b : all_bars_) {
        for (const auto& s : b.cfg.left)   if (s.kind == BarSlotKind::Lua) {
                has_lua = true; break;
            }
        for (const auto& s : b.cfg.center) if (s.kind == BarSlotKind::Lua) {
                has_lua = true; break;
            }
        for (const auto& s : b.cfg.right)  if (s.kind == BarSlotKind::Lua) {
                has_lua = true; break;
            }
        if (has_lua) break;
    }

    if (has_lua) {
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (tfd >= 0) {
            struct itimerspec ts = {};
            ts.it_value.tv_sec    = 1;
            ts.it_interval.tv_sec = 1;
            timerfd_settime(tfd, 0, &ts, nullptr);
            widget_timer_ = EventLoop::FdHandle(runtime.event_loop, tfd, [this, tfd]() {
                    uint64_t expirations;
                    if (read(tfd, &expirations, sizeof(expirations)) > 0) {
                        refresh_widgets();
                        redraw();
                    }
                });
            LOG_INFO("Bar: widget timer started (1s base tick)");
        }
    }

    redraw();
    LOG_INFO("Bar: initialized, %d bar window(s)", (int)all_bars_.size());
}
