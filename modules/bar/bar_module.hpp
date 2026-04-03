#pragma once

#include <module.hpp>
#include <bar_config.hpp>
#include <bar/bar_state.hpp>
#include <bar/widgets/bar_widgets.hpp>
#include <backend/render_port.hpp>
#include <backend/tray_host.hpp>

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <utility>
#include <unordered_map>

// ---------------------------------------------------------------------------
// BarModule — module entry point
// ---------------------------------------------------------------------------

class BarModule : public Module {
    public:
        explicit BarModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "bar"; }
        void on_init(Core&)  override;
        void on_lua_init(Core&) override;
        void on_start(Core&) override;
        void on_stop(Core&, bool is_exec_restart = false) override;
        ~BarModule();

        void on_reload(Core&) override;
        void on(Core&, event::WindowMapped) override { redraw(); }
        void on(Core&, event::WindowUnmapped) override;
        void on(Core&, event::FocusChanged) override { redraw(); }
        void on(Core&, event::WorkspaceSwitched) override { redraw(); }
        void on(Core&, event::RaiseDocks) override { raise_all(); redraw(); }
        void on(Core& core, event::DisplayTopologyChanged) override;
        void on(Core&, event::ExposeWindow ev) override;
        void on(Core&, event::ButtonEv ev) override;
        bool on(Core&, event::ClientMessageEv ev) override;
        void on(Core&, event::DestroyNotify ev) override;
        void on(Core&, event::ConfigureNotify ev) override;
        void on(Core&, event::PropertyNotify ev) override;
        void on(Core&, event::WindowAssignedToWorkspace) override { rebalance_tray_icons(); redraw(); }
        void on(Core&, event::TrayIconDocked ev) override;

    private:
        BarConfig top_cfg;
        BarConfig bottom_cfg;
        // Non-owning. Set in on_start(), valid until on_stop().
        Core* core_ref = nullptr;
        // Non-owning. Obtained from Backend::render_port() in on_start().
        backend::RenderPort* render_port = nullptr;
        std::vector<std::unique_ptr<backend::RenderWindow> > bars;
        std::vector<std::unique_ptr<backend::RenderWindow> > bottom_bars;

        // One TrayHost per monitor. Exactly one slot has owns_selection=true (the owner).
        struct TraySlot {
            int                                mon_idx = -1;
            std::unique_ptr<backend::TrayHost> tray;
        };
        std::vector<TraySlot> trays;

        std::unordered_map<WindowId, std::vector<bar::widgets::TagHit> > tag_hits;
        bar::widgets::TagsWidget tags_widget;
        bar::widgets::TitleWidget title_widget;
        bar::widgets::TrayWidget tray_widget;

        std::function<BarState(int mon_idx)> state_provider;

        // Cached Lua widget results; refreshed per-widget according to interval.
        std::unordered_map<std::string, std::string> widget_cache;
        // Per-widget tick counter: ticks since last refresh.
        std::unordered_map<std::string, int> widget_ticks;

        int wakeup_pipe[2] = { -1, -1 };
        int timer_fd       = -1;            // timerfd for widget refresh (1s base tick)

        struct MonRect { int idx, x, y, w; };

        // Returns the TrayHost for a given monitor index, or nullptr.
        backend::TrayHost*       tray_for_monitor(int mon_idx);
        const backend::TrayHost* tray_for_monitor(int mon_idx) const;
        // Returns the single owner tray (owns _NET_SYSTEM_TRAY_S), or nullptr.
        backend::TrayHost*       owner_tray();

        int monitor_for_icon(WindowId icon_win) const;
        // Move icon to the tray on the correct monitor using xcb_reparent_window.
        // Never changes selection ownership.
        void route_icon_to_monitor(WindowId icon_win, int mon_idx);
        void rebalance_tray_icons();
        void rebuild_trays(Core& core);
        void create_bars(int bar_h, const std::vector<MonRect>& monitors);
        void create_bottom_bars(int bar_h, const std::vector<MonRect>& monitors);
        int  tag_at(WindowId bar_window, int click_x) const;
        void rebuild_bars(Core& core);
        void refresh_widgets();
        void redraw();
        void raise_all();
        void stop_runtime();
};