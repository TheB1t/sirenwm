#pragma once

#include <module.hpp>
#include <runtime.hpp>
#include <bar_config.hpp>
#include <runtime_store.hpp>
#include <bar/bar_state.hpp>
#include <bar/widgets/bar_widgets.hpp>
#include <backend/tray_host.hpp>
#include <surface.hpp>

#include <optional>
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
        void on_init()     override;
        void on_lua_init() override;
        bool parse_setup(LuaContext& lua, int table_idx, std::string& err);
        void on_start() override;
        void on_stop(bool is_exec_restart = false) override;
        ~BarModule();

        void on_reload() override;
        using Module::on;
        void on(event::RuntimeStarted) override { refresh_widgets(); redraw(); }
        void on(event::WindowMapped) override { redraw(); }
        void on(event::WindowUnmapped) override;
        void on(event::FocusChanged) override { redraw(); }
        void on(event::WorkspaceSwitched) override { redraw(); }
        void on(event::RaiseDocks) override { raise_all(); redraw(); }
        void on(event::DisplayTopologyChanged) override;
        void on(event::ExposeSurface ev) override;
        void on(event::SurfaceButton ev) override;
        void on(event::ButtonEv ev) override;
        bool on(event::ClientMessageEv ev) override;
        void on(event::DestroyNotify ev) override;
        void on(event::ConfigureNotify ev) override;
        void on(event::PropertyNotify ev) override;
        void on(event::WindowAssignedToWorkspace) override { rebalance_tray_icons(); redraw(); }
        void on(const event::CustomEvent& ev) override;

    private:
        // Owned setting registered in RuntimeStore.
        TypedSetting<BarSetConfig> bar_set_setting_;

        BarSetConfig bar_set_cfg_;

        // Active bar windows. Each entry carries its resolved config.
        struct BarWindow {
            std::unique_ptr<Surface>           surface;
            BarConfig                          cfg; // resolved config for this specific monitor
            bool                               is_top;
            std::unique_ptr<backend::TrayHost> tray; // attached via runtime().create_tray(); may be null
        };
        std::vector<BarWindow> all_bars_;

        // Convenience views into all_bars_ (non-owning).
        // Used where code needs to iterate top or bottom bars separately.
        std::vector<Surface*> top_bar_surfaces() const;
        std::vector<Surface*> bottom_bar_surfaces() const;

        std::unordered_map<Surface*, std::vector<bar::widgets::TagHit>> tag_hits;
        bar::widgets::TagsWidget  tags_widget;
        bar::widgets::TitleWidget title_widget;
        bar::widgets::TrayWidget  tray_widget;

        std::function<BarState(int mon_idx)> state_provider;

        int                      wakeup_pipe_wr_ = -1;  // write-end of wakeup pipe
        EventLoop::FdHandle wakeup_pipe_rd_;       // read-end, owns fd + watch
        EventLoop::FdHandle widget_timer_;         // timerfd for 1s widget tick

        struct MonRect { int idx; Vec2i pos; Vec2i size; std::string alias; };

        // Returns the TrayHost attached to the top bar on a given monitor, or nullptr.
        backend::TrayHost*       tray_for_monitor(int mon_idx);
        const backend::TrayHost* tray_for_monitor(int mon_idx) const;
        // Returns the single owner tray (owns _NET_SYSTEM_TRAY_S), or nullptr.
        backend::TrayHost*       owner_tray();

        int                      monitor_for_icon(WindowId icon_win) const;
        // Move icon to the tray on the correct monitor using xcb_reparent_window.
        // Never changes selection ownership.
        void        route_icon_to_monitor(WindowId icon_win, int mon_idx);
        void        rebalance_tray_icons();
        void        rebuild_trays();
        // Resolve alias for a monitor index using current_settings().monitor_aliases.
        std::string monitor_alias(int mon_idx) const;
        // Create a single bar window and push into all_bars_.
        void        create_bar_window(const MonRect& m, const BarConfig& cfg, bool is_top);
        int         tag_at(const Surface* surface, int click_x) const;
        void        rebuild_bars();
        void        refresh_widgets();
        void        redraw();
        void        raise_all();
        void        stop_runtime();
};
