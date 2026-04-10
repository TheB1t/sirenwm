#include <wl_backend.hpp>
#include <wl_surface.hpp>

#include <core.hpp>
#include <log.hpp>
#include <runtime.hpp>

#include <backend/commands.hpp>

// ---------------------------------------------------------------------------
// handle_new_xdg_surface
//
// Called when a client creates a new xdg_surface.
// Popups: delegated to wlroots scene, not managed by Core.
// Toplevels: pre-created here and staged in pending_; EnsureWindow→create_window
// transfers ownership to Core.
// ---------------------------------------------------------------------------
void WaylandBackend::handle_new_xdg_surface(wlr_xdg_surface* xdg_surface) {
    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        // Popup: insert into scene automatically; no Core management.
        wlr_scene_xdg_surface_create(scene_root(), xdg_surface);
        return;
    }

    wlr_xdg_toplevel* toplevel = xdg_surface->toplevel;
    LOG_INFO("WaylandBackend: new xdg-toplevel app_id='%s' title='%s'",
        toplevel->app_id ? toplevel->app_id : "",
        toplevel->title  ? toplevel->title  : "");

    // Create scene tree node for this surface.
    wlr_scene_tree* scene_tree = wlr_scene_xdg_surface_create(scene_root(), xdg_surface);

    // Allocate a WindowId and pre-create the WlSurface.
    WindowId id = alloc_window_id();

    auto     surf = std::make_shared<WlSurface>(toplevel, scene_tree);
    surf->id = id;

    WlSurface* raw = surf.get();

    // Stage in pending_ — Core will pick it up via create_window(id).
    pending_[id] = std::move(surf);

    // Wire xdg_surface lifecycle signals.
    // In wlroots 0.18 map/unmap moved from xdg_surface to xdg_surface->surface.
    raw->on_map_.connect(&xdg_surface->surface->events.map,
        [this, raw](void*) { handle_surface_map(raw); });
    raw->on_unmap_.connect(&xdg_surface->surface->events.unmap,
        [this, raw](void*) { handle_surface_unmap(raw); });
    raw->on_destroy_.connect(&xdg_surface->events.destroy,
        [this, raw](void*) { handle_surface_destroy(raw); });
    raw->on_commit_.connect(&xdg_surface->surface->events.commit,
        [raw](void*) { raw->on_commit(); });
    raw->on_set_title_.connect(&toplevel->events.set_title,
        [this, raw](void*) { runtime_.emit(event::PropertyNotify{ raw->id, 0 }); });
    raw->on_set_app_id_.connect(&toplevel->events.set_app_id,
        [this, raw](void*) { runtime_.emit(event::PropertyNotify{ raw->id, 1 }); });

    // Client-requested fullscreen toggle.
    raw->on_request_fullscreen_.connect(&toplevel->events.request_fullscreen,
        [this, raw](void*) {
            bool want = raw->toplevel()->requested.fullscreen;
            core_.dispatch(command::SetWindowFullscreen{ raw->id, want, false });
        });

    // Client-requested maximize: treat as fullscreen for tiling WMs.
    raw->on_request_maximize_.connect(&toplevel->events.request_maximize,
        [this, raw](void*) {
            bool want = raw->toplevel()->requested.maximized;
            core_.dispatch(command::SetWindowFullscreen{ raw->id, want, false });
        });

    // Client-requested interactive move/resize (e.g. title-bar drag).
    // We acknowledge but don't initiate — mouse bindings handle pointer-driven
    // move/resize in Core.  Send an empty configure so the client doesn't hang.
    raw->on_request_move_.connect(&toplevel->events.request_move,
        [raw](void*) { wlr_xdg_surface_schedule_configure(raw->xdg_surface()); });
    raw->on_request_resize_.connect(&toplevel->events.request_resize,
        [raw](void*) { wlr_xdg_surface_schedule_configure(raw->xdg_surface()); });
}

// ---------------------------------------------------------------------------
// handle_surface_map — xdg-toplevel committed its first frame and is ready.
// ---------------------------------------------------------------------------
void WaylandBackend::handle_surface_map(WlSurface* surf) {
    surf->mapped = true;
    wlr_scene_node_set_enabled(&surf->scene_node()->node, true);

    LOG_INFO("WaylandBackend: surface %u mapped", surf->id);

    // Register with Core (calls back into create_window(surf->id) which moves
    // the surface from pending_ into surfaces_).
    (void)core_.dispatch(command::EnsureWindow{ .window = surf->id });

    // Let rules/policy decide workspace, floating, fullscreen, etc.
    runtime_.emit(event::ApplyWindowRules{ surf->id });
    runtime_.emit(event::WindowMapped{ surf->id });
}

// ---------------------------------------------------------------------------
// handle_surface_unmap — client withdrew the surface temporarily.
// ---------------------------------------------------------------------------
void WaylandBackend::handle_surface_unmap(WlSurface* surf) {
    surf->mapped = false;
    wlr_scene_node_set_enabled(&surf->scene_node()->node, false);

    LOG_INFO("WaylandBackend: surface %u unmapped", surf->id);
    runtime_.emit(event::WindowUnmapped{ surf->id, false });
}

// ---------------------------------------------------------------------------
// handle_surface_destroy — client destroyed the xdg_surface entirely.
// ---------------------------------------------------------------------------
void WaylandBackend::handle_surface_destroy(WlSurface* surf) {
    LOG_INFO("WaylandBackend: surface %u destroyed", surf->id);

    // Emit destroy before cleanup so Core can process the removal.
    runtime_.emit(event::DestroyNotify{ surf->id });

    // Remove from both maps (one of them will be empty depending on whether
    // the surface was ever mapped).
    pending_.erase(surf->id);
    surfaces_.erase(surf->id);
}
