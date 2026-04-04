#include "bar_module.hpp"

#include <module_registry.hpp>
#include <config.hpp>
#include <core.hpp>
#include <log.hpp>
#include <backend/backend.hpp>
#include <runtime.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>

backend::TrayHost* BarModule::tray_for_monitor(int mon_idx) {
    for (auto& slot : trays)
        if (slot.mon_idx == mon_idx)
            return slot.tray.get();
    return nullptr;
}

const backend::TrayHost* BarModule::tray_for_monitor(int mon_idx) const {
    for (const auto& slot : trays)
        if (slot.mon_idx == mon_idx)
            return slot.tray.get();
    return nullptr;
}

backend::TrayHost* BarModule::owner_tray() {
    for (auto& slot : trays)
        if (slot.tray && slot.tray->owns_selection())
            return slot.tray.get();
    return nullptr;
}

int BarModule::monitor_for_icon(WindowId icon_win) const {
    if (!core_ref)
        return bars.empty() ? 0 : bars.front()->monitor_index();

    // Default: keep icon where it currently is.
    int         fallback = -1;
    std::string icon_class;
    for (const auto& slot : trays) {
        if (slot.tray && slot.tray->contains_icon(icon_win)) {
            icon_class = slot.tray->icon_wm_class(icon_win);
            fallback   = slot.mon_idx;
            break;
        }
    }
    if (fallback < 0)
        fallback = bars.empty() ? 0 : bars.front()->monitor_index();

    // No WM_CLASS — can't match to any window, leave icon where it is.
    if (icon_class.empty())
        return fallback;

    const auto& mons = core_ref->monitor_states();
    std::unordered_map<int, int> ws_owner_mon;
    for (int mon_idx = 0; mon_idx < (int)mons.size(); mon_idx++)
        for (int ws_id : core_ref->monitor_workspace_ids(mon_idx))
            ws_owner_mon[ws_id] = mon_idx;

    int best_mon   = -1;
    int best_score = -1;
    for (auto win : core_ref->all_window_ids()) {
        auto w = core_ref->window_state_any(win);
        if (!w)
            continue;

        auto base_of = [](const std::string& s) {
                auto dot = s.find('.');
                return dot != std::string::npos ? s.substr(0, dot) : s;
            };
        std::string wc    = w->wm_class;
        for (auto& c : wc) c = (char)tolower((unsigned char)c);
        std::string wi    = w->wm_instance;
        for (auto& c : wi) c = (char)tolower((unsigned char)c);
        bool        match = (wc == icon_class) || (wi == icon_class)
            || (base_of(wc) == icon_class) || (wc == base_of(icon_class))
            || (base_of(wi) == icon_class) || (wi == base_of(icon_class));
        if (!match)
            continue;

        int  ws_id     = core_ref->workspace_of_window(win);
        auto it_owner  = ws_owner_mon.find(ws_id);
        int  owner_mon = (it_owner != ws_owner_mon.end()) ? it_owner->second : -1;

        int  score     = 1;
        if (w->visible)
            score += 100;
        if (owner_mon >= 0 && core_ref->active_workspace_on_monitor(owner_mon) == ws_id)
            score += 10;
        if (owner_mon == core_ref->focused_monitor_index())
            score += 1;

        if (owner_mon >= 0 && score > best_score) {
            best_score = score;
            best_mon   = owner_mon;
        }
    }

    return (best_mon >= 0) ? best_mon : fallback;
}

void BarModule::route_icon_to_monitor(WindowId icon_win, int target_mon) {
    // Find which tray currently holds this icon.
    backend::TrayHost* src = nullptr;
    for (auto& slot : trays) {
        if (slot.tray && slot.tray->contains_icon(icon_win)) {
            src = slot.tray.get();
            break;
        }
    }
    if (!src)
        return;

    backend::TrayHost* dst = tray_for_monitor(target_mon);
    if (!dst || dst == src)
        return;

    LOG_INFO("Bar: transfer icon 0x%x -> monitor %d", icon_win, target_mon);
    src->transfer_icon_to(*dst, icon_win);
}

void BarModule::rebalance_tray_icons() {
    if (trays.empty())
        return;
    // Snapshot all icons across all trays first (transfer modifies lists).
    std::vector<std::pair<WindowId, int> > to_route;
    for (auto& slot : trays) {
        if (!slot.tray)
            continue;
        for (auto icon_win : slot.tray->icon_windows()) {
            int target = monitor_for_icon(icon_win);
            if (target != slot.mon_idx)
                to_route.emplace_back(icon_win, target);
        }
    }
    for (auto& [icon_win, target] : to_route)
        route_icon_to_monitor(icon_win, target);
}

int BarModule::tag_at(WindowId bar_window, int click_x) const {
    auto it = tag_hits.find(bar_window);
    if (it == tag_hits.end())
        return -1;
    for (const auto& h : it->second) {
        if (click_x >= h.x0 && click_x < h.x1)
            return h.ws_id;
    }
    return -1;
}

void BarModule::refresh_widgets() {
    for (const auto& w : config().get_bar_widgets()) {
        int& ticks = widget_ticks[w.name];
        // interval=0 means refresh every redraw (handled in redraw directly)
        if (w.interval <= 0)
            continue;
        ticks++;
        if (ticks < w.interval)
            continue;
        ticks = 0;
        std::string result;
        config().lua().call_ref_string(w.callback, result,
            ("bar.widget '" + w.name + "'").c_str());
        widget_cache[w.name] = std::move(result);
    }
}

void BarModule::redraw() {
    if (!state_provider)
        return;

    // For interval=0 widgets, call Lua every redraw (fast/reactive widgets).
    for (const auto& w : config().get_bar_widgets()) {
        if (w.interval != 0)
            continue;
        std::string result;
        config().lua().call_ref_string(w.callback, result,
            ("bar.widget '" + w.name + "'").c_str());
        widget_cache[w.name] = std::move(result);
    }

    auto resolve_text = [&](const std::string& name) -> std::string {
            auto it = widget_cache.find(name);
            return (it != widget_cache.end()) ? it->second : std::string{};
        };

    auto render_left_slot = [&](bar::widgets::PaintContext& paint,
        const BarState& state, const BarConfig& cfg,
        const BarSlot& slot, int& cursor,
        backend::TrayHost* tray) -> std::vector<bar::widgets::TagHit> {
            if (slot.name == "tags")
                return tags_widget.draw(paint, state, cfg, cursor);
            if (slot.name == "tray") {
                cursor += tray_widget.reserved_width(tray);
                return {};
            }
            std::string text = resolve_text(slot.name);
            if (!text.empty())
                cursor += paint.draw_text(cursor, text,
                        cfg.colors.status_fg, cfg.colors.bar_bg);
            return {};
        };

    auto render_right_slot = [&](bar::widgets::PaintContext& paint,
        const BarState& state, const BarConfig& cfg,
        const BarSlot& slot, int right,
        backend::TrayHost* tray) -> int {
            if (slot.name == "tray")
                return right - tray_widget.reserved_width(tray);
            if (slot.name == "tags")
                return right; // unsupported in right zone
            std::string text = resolve_text(slot.name);
            if (!text.empty()) {
                int sw = paint.text_width(text) + 16;
                paint.draw_text(right - sw, text,
                    cfg.colors.status_fg, cfg.colors.bar_bg);
                return right - sw;
            }
            return right;
        };

    auto render_center_slot = [&](bar::widgets::PaintContext& paint,
        const BarState& state, const BarConfig& cfg,
        const BarSlot& slot, int left_edge, int right_edge) {
            if (slot.name == "title") {
                title_widget.draw_center(paint, state, cfg, left_edge, right_edge);
                return;
            }
            std::string text = resolve_text(slot.name);
            if (!text.empty()) {
                int tw        = paint.text_width(text) + 16;
                int available = right_edge - left_edge;
                int cx        = left_edge + (available - tw) / 2;
                cx = std::max(left_edge, std::min(cx, right_edge - tw));
                if (cx + tw <= right_edge)
                    paint.draw_text(cx, text,
                        cfg.colors.normal_fg, cfg.colors.bar_bg);
            }
        };

    auto has_tray_slot = [](const BarConfig& cfg) {
            for (const auto& s : cfg.left)   if (s.name == "tray") return true;
            for (const auto& s : cfg.right)  if (s.name == "tray") return true;
            for (const auto& s : cfg.center) if (s.name == "tray") return true;
            return false;
        };

    auto render_bar_set = [&](
        std::vector<std::unique_ptr<backend::RenderWindow> >& bar_set,
        const BarConfig& cfg, bool is_top) {
            bool tray_in_zone = has_tray_slot(cfg);
            for (auto& bar : bar_set) {
                backend::TrayHost* bar_tray = tray_in_zone
                    ? tray_for_monitor(bar->monitor_index()) : nullptr;

                BarState state = state_provider(bar->monitor_index());
                bar::widgets::PaintContext paint(*bar, cfg.font);
                paint.clear(cfg.colors.bar_bg);

                int left_cursor = 0;
                std::vector<bar::widgets::TagHit> hits;
                for (const auto& slot : cfg.left) {
                    auto h = render_left_slot(paint, state, cfg, slot, left_cursor, bar_tray);
                    hits.insert(hits.end(), h.begin(), h.end());
                }

                int right_cursor = paint.width();
                for (int i = (int)cfg.right.size() - 1; i >= 0; i--)
                    right_cursor = render_right_slot(paint, state, cfg, cfg.right[i], right_cursor, bar_tray);

                for (const auto& slot : cfg.center)
                    render_center_slot(paint, state, cfg, slot, left_cursor, right_cursor);

                paint.present();
                if (!hits.empty())
                    tag_hits.emplace(bar->id(), std::move(hits));
                if (bar_tray)
                    tray_widget.reposition(bar_tray, *bar);
            }
        };

    tag_hits.clear();
    render_bar_set(bars, top_cfg, true);
    render_bar_set(bottom_bars, bottom_cfg, false);
}

void BarModule::raise_all() {
    for (auto& b : bars) {
        backend::TrayHost* t = tray_for_monitor(b->monitor_index());
        if (core_ref && (core_ref->monitor_has_visible_fullscreen(b->monitor_index()) ||
            core_ref->monitor_has_visible_borderless(b->monitor_index()))) {
            b->lower();
            if (t) t->raise(b->id()); // keep tray visible above fullscreen/borderless
            continue;
        }
        b->raise();
        if (t) t->raise(b->id());
    }

    for (auto& b : bottom_bars) {
        if (core_ref && (core_ref->monitor_has_visible_fullscreen(b->monitor_index()) ||
            core_ref->monitor_has_visible_borderless(b->monitor_index()))) {
            b->lower();
            continue;
        }
        b->raise();
    }
}

void BarModule::create_bars(int bar_h, const std::vector<MonRect>& mons) {
    if (!render_port)
        return;
    for (const auto& m : mons) {
        backend::RenderWindowCreateInfo info;
        info.monitor_index           = m.idx;
        info.x                       = m.x;
        info.y                       = m.y;
        info.width                   = m.w;
        info.height                  = bar_h;
        info.background_pixel        = render_port->black_pixel();
        info.want_expose             = true;
        info.want_button_press       = true;
        info.want_button_release     = true;
        info.hints.override_redirect = true;
        info.hints.dock              = true;
        info.hints.keep_above        = true;

        auto bar = render_port->create_window(info);
        if (!bar)
            continue;
        // No strut: apps calculate their usable area from _NET_WM_STRUT.
        // We manage the inset purely inside Core so games see the full monitor.
        bars.push_back(std::move(bar));
    }
}

void BarModule::create_bottom_bars(int bar_h, const std::vector<MonRect>& mons) {
    if (!render_port)
        return;
    for (const auto& m : mons) {
        backend::RenderWindowCreateInfo info;
        info.monitor_index           = m.idx;
        info.x                       = m.x;
        info.y                       = m.y;    // caller sets correct y (monitor bottom - bar_h)
        info.width                   = m.w;
        info.height                  = bar_h;
        info.background_pixel        = render_port->black_pixel();
        info.want_expose             = true;
        info.want_button_press       = true;
        info.want_button_release     = true;
        info.hints.override_redirect = true;
        info.hints.dock              = true;
        info.hints.keep_above        = true;

        auto bar = render_port->create_window(info);
        if (!bar)
            continue;
        // No strut — see create_bars().
        bottom_bars.push_back(std::move(bar));
    }
}

BarModule::~BarModule() {
    stop_runtime();
}

void BarModule::on_stop(Core&, bool) {
    stop_runtime();
}

void BarModule::stop_runtime() {
    if (wakeup_pipe[0] >= 0) {
        close(wakeup_pipe[0]);
        wakeup_pipe[0] = -1;
    }
    if (wakeup_pipe[1] >= 0) {
        close(wakeup_pipe[1]);
        wakeup_pipe[1] = -1;
    }
    if (timer_fd >= 0) {
        close(timer_fd);
        timer_fd = -1;
    }
    trays.clear();
    tag_hits.clear();
    bars.clear();
    render_port = nullptr;
    core_ref    = nullptr;
}

static bool _swm_registered_lua_symbols_bar = []() {
        module_registry_static::add_lua_symbol_registration("bar", "bar");
        return true;
    }();

SWM_REGISTER_MODULE("bar", BarModule)