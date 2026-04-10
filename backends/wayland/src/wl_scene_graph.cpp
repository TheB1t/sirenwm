#include <wl_scene_graph.hpp>
#include <wl_compat.hpp>
#include <log.hpp>

WlSceneGraph::WlSceneGraph(wl_display* display) {
    layout_ = wlr_output_layout_create(display);
    if (!layout_)
        LOG_ERR("WlSceneGraph: wlr_output_layout_create failed");

    scene_ = wlr_scene_create();
    if (!scene_)
        LOG_ERR("WlSceneGraph: wlr_scene_create failed");

    wlr_scene_attach_output_layout(scene_, layout_);
}

WlSceneGraph::~WlSceneGraph() {
    // wlr_scene has no destroy function — freed via wl_display_destroy globals
    if (layout_)
        wlr_output_layout_destroy(layout_);
}

wlr_scene_tree* WlSceneGraph::root() const noexcept {
    return wlr_compat::scene_root(scene_);
}
