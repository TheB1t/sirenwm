#pragma once

#include <wl_listener.hpp>

#include <functional>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
}

// ---------------------------------------------------------------------------
// WlXdgShell — thin RAII owner of wlr_xdg_shell.
//
// Creates the xdg-shell global and wires the new_surface signal on construction.
// The wlr_xdg_shell object itself is destroyed via wl_display_destroy.
// ---------------------------------------------------------------------------
class WlXdgShell {
    public:
        using SurfaceCb = std::function<void(wlr_xdg_surface*)>;

        WlXdgShell(wl_display* display, uint32_t version, SurfaceCb on_new_surface);
        ~WlXdgShell() = default;

        // Non-copyable, non-movable.
        WlXdgShell(const WlXdgShell&)            = delete;
        WlXdgShell& operator=(const WlXdgShell&) = delete;
        WlXdgShell(WlXdgShell&&)                 = delete;
        WlXdgShell& operator=(WlXdgShell&&)      = delete;

    private:
        wlr_xdg_shell*                  shell_ = nullptr;
        WlListener<wlr_xdg_surface>     on_new_surface_;
};
