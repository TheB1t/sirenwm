#pragma once

// ---------------------------------------------------------------------------
// wl_compat.hpp — all wlroots version differences isolated in one place.
//
// Include this only in .cpp files that need version-specific behaviour.
// Nothing in wl_listener.hpp, wl_backend.hpp, or port headers should
// include this file — they must be version-agnostic.
// ---------------------------------------------------------------------------

extern "C" {
#include <wlr/types/wlr_scene.h>
#include <wlr/render/wlr_renderer.h>
}

namespace wlr_compat {

// wlr_scene::tree changed from a wlr_scene_tree value (0.17) to a
// wlr_scene_tree* pointer (0.18+).  Always call this instead of
// accessing scene->tree directly.
inline wlr_scene_tree* scene_root(wlr_scene* scene) noexcept {
#if defined(WLR_SCENE_TREE_IS_POINTER)
    return scene->tree;
#else
    return &scene->tree;
#endif
}

// True when running on a software (pixman) renderer with no DRM device.
// Used to skip xcursor upload and cursor attachment.
inline bool is_software_renderer(wlr_renderer* r) noexcept {
    return wlr_renderer_get_drm_fd(r) < 0;
}

} // namespace wlr_compat
