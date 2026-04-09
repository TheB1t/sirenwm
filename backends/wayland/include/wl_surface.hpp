#pragma once

// ---------------------------------------------------------------------------
// WlSurface — Wayland-native managed window (xdg-toplevel).
//
// Lifecycle mirrors X11Window: WlBackend creates it when an xdg-toplevel
// maps, assigns a sequential WindowId, and registers it with Core.
//
// XWayland surfaces have their own flow (real X11 window IDs).
// ---------------------------------------------------------------------------

#include <window.hpp>
#include <wl_listener.hpp>

extern "C" {
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
}

class WlSurface final : public swm::Window {
public:
    explicit WlSurface(wlr_xdg_toplevel* toplevel, wlr_scene_tree* scene_tree);
    ~WlSurface() override;

    wlr_xdg_toplevel* toplevel()   const { return toplevel_; }
    wlr_xdg_surface*  xdg_surface() const { return toplevel_->base; }
    wlr_scene_tree*   scene_node()  const { return scene_tree_; }

    // Send configure with new geometry (tiling/floating layout pass).
    // Stores pending_serial; WM waits for commit before treating as applied.
    void set_geometry(int x, int y, int w, int h);

    // Called from the xdg_surface::commit signal handler.
    void on_commit();

    bool mapped = false;

    // Listeners — wired in ctor, auto-disconnected in dtor.
    WlListener on_map_;
    WlListener on_unmap_;
    WlListener on_destroy_;
    WlListener on_commit_;
    WlListener on_request_move_;
    WlListener on_request_resize_;
    WlListener on_request_fullscreen_;
    WlListener on_request_maximize_;
    WlListener on_set_title_;
    WlListener on_set_app_id_;

private:
    wlr_xdg_toplevel* toplevel_   = nullptr;
    wlr_scene_tree*   scene_tree_ = nullptr;
    uint32_t          pending_serial_ = 0;
};
