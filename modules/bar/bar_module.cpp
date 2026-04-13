#include "bar_module.hpp"

#include <module_registry.hpp>
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
    for (auto& b : all_bars_)
        if (b.is_top && b.tray && b.surface && b.surface->monitor_index() == mon_idx)
            return b.tray.get();
    return nullptr;
}

const backend::TrayHost* BarModule::tray_for_monitor(int mon_idx) const {
    for (const auto& b : all_bars_)
        if (b.is_top && b.tray && b.surface && b.surface->monitor_index() == mon_idx)
            return b.tray.get();
    return nullptr;
}

backend::TrayHost* BarModule::owner_tray() {
    for (auto& b : all_bars_)
        if (b.tray && b.tray->owns_selection())
            return b.tray.get();
    return nullptr;
}

int BarModule::monitor_for_icon(WindowId icon_win) const {
    int         fallback = -1;
    std::string icon_class;
    for (const auto& b : all_bars_) {
        if (b.tray && b.surface && b.tray->contains_icon(icon_win)) {
            icon_class = b.tray->icon_wm_class(icon_win);
            fallback   = b.surface->monitor_index();
            break;
        }
    }
    auto top_surfaces = top_bar_surfaces();
    if (fallback < 0)
        fallback = top_surfaces.empty() ? 0 : top_surfaces.front()->monitor_index();

    if (icon_class.empty())
        return fallback;

    const auto&                  mons = core.monitor_states();
    std::unordered_map<int, int> ws_owner_mon;
    for (int mon_idx = 0; mon_idx < (int)mons.size(); mon_idx++)
        for (int ws_id : core.monitor_workspace_ids(mon_idx))
            ws_owner_mon[ws_id] = mon_idx;

    int best_mon   = -1;
    int best_score = -1;
    for (auto win : core.all_window_ids()) {
        auto w = core.window_state_any(win);
        if (!w) continue;

        auto base_of = [](const std::string& s) {
                auto dot = s.find('.');
                return dot != std::string::npos ? s.substr(0, dot) : s;
            };
        std::string wc = w->wm_class;
        for (auto& c : wc) c = (char)tolower((unsigned char)c);
        std::string wi = w->wm_instance;
        for (auto& c : wi) c = (char)tolower((unsigned char)c);
        bool        match = (wc == icon_class) || (wi == icon_class)
            || (base_of(wc) == icon_class) || (wc == base_of(icon_class))
            || (base_of(wi) == icon_class) || (wi == base_of(icon_class));
        if (!match) continue;

        int  ws_id     = core.workspace_of_window(win);
        auto it_owner  = ws_owner_mon.find(ws_id);
        int  owner_mon = (it_owner != ws_owner_mon.end()) ? it_owner->second : -1;

        // Score by visibility (100), active workspace on its monitor (10), focused monitor (1).
        int score = 1;
        if (w->is_visible())  score += 100;
        if (owner_mon >= 0 && core.active_workspace_on_monitor(owner_mon) == ws_id)
            score += 10;
        if (owner_mon == core.focused_monitor_index()) score += 1;

        if (owner_mon >= 0 && score > best_score) {
            best_score = score;
            best_mon   = owner_mon;
        }
    }
    return (best_mon >= 0) ? best_mon : fallback;
}

void BarModule::route_icon_to_monitor(WindowId icon_win, int target_mon) {
    backend::TrayHost* src = nullptr;
    for (auto& b : all_bars_) {
        if (b.tray && b.tray->contains_icon(icon_win)) {
            src = b.tray.get();
            break;
        }
    }
    if (!src) return;

    backend::TrayHost* dst = tray_for_monitor(target_mon);
    if (!dst || dst == src) return;

    LOG_INFO("Bar: transfer icon 0x%x -> monitor %d", icon_win, target_mon);
    src->transfer_icon_to(*dst, icon_win);
}

void BarModule::rebalance_tray_icons() {
    bool any_tray = false;
    for (const auto& b : all_bars_) if (b.tray) { any_tray = true; break; }
    if (!any_tray) return;
    std::vector<std::pair<WindowId, int>> to_route;
    for (auto& b : all_bars_) {
        if (!b.tray || !b.surface) continue;
        int mon_idx = b.surface->monitor_index();
        for (auto icon_win : b.tray->icon_windows()) {
            int target = monitor_for_icon(icon_win);
            if (target != mon_idx)
                to_route.emplace_back(icon_win, target);
        }
    }
    for (auto& [icon_win, target] : to_route)
        route_icon_to_monitor(icon_win, target);
}

int BarModule::tag_at(const Surface* surface, int click_x) const {
    auto it = tag_hits.find(const_cast<Surface*>(surface));
    if (it == tag_hits.end()) return -1;
    for (const auto& h : it->second)
        if (click_x >= h.x0 && click_x < h.x1)
            return h.ws_id;
    return -1;
}

static void refresh_slot(LuaHost& lua, const BarSlot& slot) {
    if (slot.kind != BarSlotKind::Lua || slot.interval <= 0) return;
    slot.ticks++;
    if (slot.ticks < slot.interval) return;
    slot.ticks = 0;
    lua.call_ref_method_string(slot.widget, "render", slot.cached_text, "bar.widget");
}

void BarModule::refresh_widgets() {
    if (runtime_state() != RuntimeState::Running) return;
    auto& lua = this->lua;
    for (const auto& b : all_bars_) {
        for (const auto& s : b.cfg.left)   refresh_slot(lua, s);
        for (const auto& s : b.cfg.center) refresh_slot(lua, s);
        for (const auto& s : b.cfg.right)  refresh_slot(lua, s);
    }
}

static void update_reactive_slot(LuaHost& lua, const BarSlot& slot) {
    if (slot.kind == BarSlotKind::Lua && slot.interval == 0)
        lua.call_ref_method_string(slot.widget, "render", slot.cached_text, "bar.widget");
}

void BarModule::redraw() {
    if (!state_provider) return;

    if (runtime_state() == RuntimeState::Running) {
        auto& lua = this->lua;
        for (const auto& b : all_bars_) {
            for (const auto& s : b.cfg.left)   update_reactive_slot(lua, s);
            for (const auto& s : b.cfg.center) update_reactive_slot(lua, s);
            for (const auto& s : b.cfg.right)  update_reactive_slot(lua, s);
        }
    }

    auto render_left_slot = [&](bar::widgets::PaintContext& paint,
        const BarState& state, const BarConfig& cfg,
        const BarSlot& slot, int& cursor,
        backend::TrayHost* tray) -> std::vector<bar::widgets::TagHit> {
            switch (slot.kind) {
                case BarSlotKind::Tags:
                    return tags_widget.draw(paint, state, cfg, cursor);
                case BarSlotKind::Tray:
                    cursor += tray_widget.reserved_width(tray);
                    return {};
                case BarSlotKind::Title:
                    return {};
                case BarSlotKind::Lua:
                    if (!slot.cached_text.empty())
                        cursor += paint.draw_text(cursor, slot.cached_text,
                                cfg.colors.status_fg, cfg.colors.bar_bg);
                    return {};
            }
            return {};
        };

    auto render_right_slot = [&](bar::widgets::PaintContext& paint,
        const BarState& state, const BarConfig& cfg,
        const BarSlot& slot, int right,
        backend::TrayHost* tray) -> int {
            switch (slot.kind) {
                case BarSlotKind::Tray:
                    return right - tray_widget.reserved_width(tray);
                case BarSlotKind::Tags:
                case BarSlotKind::Title:
                    return right;
                case BarSlotKind::Lua:
                    if (!slot.cached_text.empty()) {
                        int sw = paint.text_width(slot.cached_text) + 16;
                        paint.draw_text(right - sw, slot.cached_text,
                            cfg.colors.status_fg, cfg.colors.bar_bg);
                        return right - sw;
                    }
                    return right;
            }
            return right;
        };

    auto render_center_slot = [&](bar::widgets::PaintContext& paint,
        const BarState& state, const BarConfig& cfg,
        const BarSlot& slot, int left_edge, int right_edge) {
            switch (slot.kind) {
                case BarSlotKind::Title:
                    title_widget.draw_center(paint, state, cfg, left_edge, right_edge);
                    return;
                case BarSlotKind::Tags:
                case BarSlotKind::Tray:
                    return;
                case BarSlotKind::Lua:
                    if (!slot.cached_text.empty()) {
                        int tw        = paint.text_width(slot.cached_text) + 16;
                        int available = right_edge - left_edge;
                        int cx        = left_edge + (available - tw) / 2;
                        cx = std::max(left_edge, std::min(cx, right_edge - tw));
                        if (cx + tw <= right_edge)
                            paint.draw_text(cx, slot.cached_text,
                                cfg.colors.normal_fg, cfg.colors.bar_bg);
                    }
                    return;
            }
        };

    auto has_tray_slot = [](const BarConfig& cfg) {
            for (const auto& s : cfg.left)   if (s.kind == BarSlotKind::Tray) return true;
            for (const auto& s : cfg.right)  if (s.kind == BarSlotKind::Tray) return true;
            for (const auto& s : cfg.center) if (s.kind == BarSlotKind::Tray) return true;
            return false;
        };

    tag_hits.clear();
    for (auto& b : all_bars_) {
        if (!b.surface) continue;
        const BarConfig&   cfg      = b.cfg;
        bool               tray_in  = has_tray_slot(cfg);
        backend::TrayHost* bar_tray = tray_in
            ? tray_for_monitor(b.surface->monitor_index()) : nullptr;

        BarState                   state = state_provider(b.surface->monitor_index());
        bar::widgets::PaintContext paint(*b.surface, cfg.font);
        paint.clear(cfg.colors.bar_bg);

        int                               left_cursor = 0;
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
            tag_hits.emplace(b.surface.get(), std::move(hits));
        if (bar_tray)
            tray_widget.reposition(bar_tray, *b.surface);
    }
}

void BarModule::raise_all() {
    for (auto& b : all_bars_) {
        if (!b.surface) continue;
        int                mon_idx = b.surface->monitor_index();
        backend::TrayHost* t       = b.is_top ? tray_for_monitor(mon_idx) : nullptr;

        if (core.monitor_has_visible_covering_window(mon_idx)) {
            b.surface->lower();
            if (t) t->raise();
            continue;
        }
        b.surface->raise();
        if (t) t->raise();
    }
}

BarModule::~BarModule() {
    stop_runtime();
}

void BarModule::on_stop(bool) {
    stop_runtime();
}

void BarModule::stop_runtime() {
    wakeup_pipe_rd_.reset();
    widget_timer_.reset();
    if (wakeup_pipe_wr_ >= 0) {
        close(wakeup_pipe_wr_); wakeup_pipe_wr_ = -1;
    }
    tag_hits.clear();
    all_bars_.clear();
}

SIRENWM_REGISTER_MODULE("bar", BarModule)
