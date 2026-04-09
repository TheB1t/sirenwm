#include <wl_surface.hpp>
#include <log.hpp>

WlSurface::WlSurface(wlr_xdg_toplevel* toplevel, wlr_scene_tree* scene_tree)
    : toplevel_(toplevel), scene_tree_(scene_tree) {}

WlSurface::~WlSurface() {
    // Listeners disconnect themselves in WlListener dtor.
}

void WlSurface::set_geometry(int x, int y, int w, int h) {
    if (!toplevel_)
        return;

    // Move the scene node to the new position.
    wlr_scene_node_set_position(&scene_tree_->node, x, y);

    // Send configure to the client with the requested size.
    // The client may adjust size; we treat the committed size as authoritative.
    pending_serial_ = wlr_xdg_toplevel_set_size(toplevel_, (uint32_t)w, (uint32_t)h);
}

void WlSurface::on_commit() {
    if (!mapped)
        return;
    // Geometry is updated through Core dispatch; nothing extra needed here.
}
