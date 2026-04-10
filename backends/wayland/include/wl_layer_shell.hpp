#pragma once

#ifndef SIRENWM_NO_LAYER_SHELL

#include <wl_listener.hpp>

#include <functional>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
}

// ---------------------------------------------------------------------------
// WlLayerShell — thin RAII owner of wlr_layer_shell_v1.
//
// Creates the layer-shell global and wires the new_surface signal.
// The wlr_layer_shell_v1 object is destroyed via wl_display_destroy.
// ---------------------------------------------------------------------------
class WlLayerShell {
    public:
        using SurfaceCb = std::function<void(wlr_layer_surface_v1*)>;

        WlLayerShell(wl_display* display, uint32_t version, SurfaceCb on_new_surface);
        ~WlLayerShell() = default;

        // Non-copyable, non-movable.
        WlLayerShell(const WlLayerShell&)            = delete;
        WlLayerShell& operator=(const WlLayerShell&) = delete;
        WlLayerShell(WlLayerShell&&)                 = delete;
        WlLayerShell& operator=(WlLayerShell&&)      = delete;

    private:
        wlr_layer_shell_v1*                  shell_ = nullptr;
        WlListener<wlr_layer_surface_v1>     on_new_surface_;
};

#endif // SIRENWM_NO_LAYER_SHELL
