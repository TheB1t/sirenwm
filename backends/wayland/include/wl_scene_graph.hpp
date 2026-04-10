#pragma once

// ---------------------------------------------------------------------------
// WlSceneGraph — RAII owner of wlr_scene + wlr_output_layout.
//
// Also hides the wlr_scene::tree 0.17/0.18 API difference:
// root() always returns wlr_scene_tree*, regardless of wlroots version.
// ---------------------------------------------------------------------------

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
}

class WlSceneGraph {
public:
    explicit WlSceneGraph(wl_display* display);
    ~WlSceneGraph();

    WlSceneGraph(const WlSceneGraph&)            = delete;
    WlSceneGraph& operator=(const WlSceneGraph&) = delete;

    wlr_scene*         scene()        const noexcept { return scene_; }
    wlr_output_layout* output_layout() const noexcept { return layout_; }

    // Returns the root scene tree, hiding the 0.17/0.18 API difference.
    wlr_scene_tree* root() const noexcept;

private:
    wlr_output_layout* layout_ = nullptr;
    wlr_scene*         scene_  = nullptr;
};
