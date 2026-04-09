#include <backend/render_port.hpp>
#include <wl_cpu_buffer.hpp>
#include <log.hpp>

#include <algorithm>
#include <memory>

extern "C" {
#include <wlr/types/wlr_scene.h>
#include <wlr/render/wlr_renderer.h>
}

// ---------------------------------------------------------------------------
// WlRenderWindow — internal compositor surface for bar/overlay drawing.
//
// Rendering flow:
//   1. Caller draws into cairo_t* returned by cairo().
//   2. Caller calls present() when the frame is complete.
//   3. present() calls wlr_scene_buffer_set_buffer() so the scene picks it up.
//   4. On the next output frame signal, wlr_scene_output_commit() composites it.
// ---------------------------------------------------------------------------

namespace backend::wl {

class WlRenderWindow final : public RenderWindow {
public:
    WlRenderWindow(wlr_scene_tree* root, const RenderWindowCreateInfo& info)
        : monitor_index_(info.monitor_index),
          x_(info.pos.x()),
          y_(info.pos.y()),
          w_(std::max(1, info.size.x())),
          h_(std::max(1, info.size.y())) {

        buf_ = WlCpuBuffer::create(w_, h_);
        if (!buf_) {
            LOG_ERR("WlRenderWindow: WlCpuBuffer::create(%d,%d) failed", w_, h_);
            return;
        }

        // Create a scene buffer node under the root tree
        tree_  = wlr_scene_tree_create(root);
        sbuf_  = wlr_scene_buffer_create(&tree_->node, &buf_->base);
        wlr_scene_node_set_position(&tree_->node, x_, y_);
    }

    ~WlRenderWindow() override {
        if (sbuf_)
            wlr_scene_node_destroy(&sbuf_->node);
        if (tree_)
            wlr_scene_node_destroy(&tree_->node);
        // buf_ is owned by the wlr_buffer ref-count: it was locked by scene.
        // When scene_node is destroyed, it drops the ref and WlCpuBuffer::destroy_impl fires.
    }

    WindowId id() const override { return NO_WINDOW; }  // internal surface, no Core window

    int monitor_index() const override { return monitor_index_; }
    int x() const override { return x_; }
    int y() const override { return y_; }
    int width() const override { return w_; }
    int height() const override { return h_; }

    cairo_t* cairo() override {
        if (!buf_) return nullptr;
        return buf_->cairo_ctx;
    }

    void present() override {
        if (!sbuf_ || !buf_) return;
        // Flush cairo to ensure pixel writes are visible
        cairo_surface_flush(buf_->cairo_surface);
        // Re-set the buffer on the scene node to trigger a damage region update
        wlr_scene_buffer_set_buffer(sbuf_, &buf_->base);
    }

    void set_visible(bool visible) override {
        if (tree_)
            wlr_scene_node_set_enabled(&tree_->node, visible);
    }

    void raise() override {
        if (tree_)
            wlr_scene_node_raise_to_top(&tree_->node);
    }

    void lower() override {
        if (tree_)
            wlr_scene_node_lower_to_bottom(&tree_->node);
    }

    void move_to(int x, int y) override {
        x_ = x; y_ = y;
        if (tree_)
            wlr_scene_node_set_position(&tree_->node, x_, y_);
    }

    void reserve_top_strut(int /*strut_height*/, int /*x_start*/, int /*x_end*/) override {
        // TODO: EWMH strut equivalent for Wayland
        // Layer-shell handles this via exclusive_zone for external bars;
        // for internal bars the runtime already knows the reserved space.
    }

    void reserve_bottom_strut(int /*strut_height*/, int /*x_start*/, int /*x_end*/) override {
        // TODO: same as reserve_top_strut
    }

private:
    int             monitor_index_;
    int             x_, y_, w_, h_;
    WlCpuBuffer*    buf_   = nullptr;  // owned by wlr_buffer refcount
    wlr_scene_tree* tree_  = nullptr;
    wlr_scene_buffer* sbuf_ = nullptr;
};

// ---------------------------------------------------------------------------
// WlRenderPort
// ---------------------------------------------------------------------------
class WlRenderPort final : public RenderPort {
public:
    WlRenderPort(wlr_scene* scene, wlr_renderer* renderer)
        : scene_(scene), renderer_(renderer) {}

    std::unique_ptr<RenderWindow> create_window(const RenderWindowCreateInfo& info) override {
        return std::make_unique<WlRenderWindow>(scene_->tree, info);
    }

    uint32_t black_pixel() const override { return 0xFF000000u; }

private:
    wlr_scene*    scene_;
    wlr_renderer* renderer_;
};

std::unique_ptr<RenderPort> create_render_port(wlr_scene* scene, wlr_renderer* renderer) {
    return std::make_unique<WlRenderPort>(scene, renderer);
}

} // namespace backend::wl
