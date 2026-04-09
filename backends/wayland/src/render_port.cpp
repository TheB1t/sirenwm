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
// In wlroots <0.18: backed by a custom wlr_buffer (WlCpuBuffer).
// In wlroots 0.18+: wlr_buffer_impl is opaque; bar rendering is a stub.
//                   TODO: implement via wl_shm / wlr_allocator in 0.18.
// ---------------------------------------------------------------------------

namespace backend::wl {

#ifndef WLR_BUFFER_IMPL_OPAQUE

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

        tree_ = wlr_scene_tree_create(root);
        sbuf_ = wlr_scene_buffer_create(tree_, &buf_->base);
        wlr_scene_node_set_position(&tree_->node, x_, y_);
    }

    ~WlRenderWindow() override {
        if (sbuf_)
            wlr_scene_node_destroy(&sbuf_->node);
        if (tree_)
            wlr_scene_node_destroy(&tree_->node);
        // buf_ lifetime managed by wlr_buffer refcount; destroy_impl fires on drop
    }

    WindowId id() const override { return NO_WINDOW; }
    int monitor_index() const override { return monitor_index_; }
    int x() const override { return x_; }
    int y() const override { return y_; }
    int width() const override { return w_; }
    int height() const override { return h_; }

    cairo_t* cairo() override { return buf_ ? buf_->cairo_ctx : nullptr; }

    void present() override {
        if (!sbuf_ || !buf_) return;
        cairo_surface_flush(buf_->cairo_surface);
        wlr_scene_buffer_set_buffer(sbuf_, &buf_->base);
    }

    void set_visible(bool visible) override {
        if (tree_) wlr_scene_node_set_enabled(&tree_->node, visible);
    }

    void raise() override {
        if (tree_) wlr_scene_node_raise_to_top(&tree_->node);
    }

    void lower() override {
        if (tree_) wlr_scene_node_lower_to_bottom(&tree_->node);
    }

    void move_to(int x, int y) override {
        x_ = x; y_ = y;
        if (tree_) wlr_scene_node_set_position(&tree_->node, x_, y_);
    }

    void reserve_top_strut(int, int, int) override {}
    void reserve_bottom_strut(int, int, int) override {}

private:
    int               monitor_index_;
    int               x_, y_, w_, h_;
    WlCpuBuffer*      buf_  = nullptr;
    wlr_scene_tree*   tree_ = nullptr;
    wlr_scene_buffer* sbuf_ = nullptr;
};

#else // WLR_BUFFER_IMPL_OPAQUE — wlroots 0.18+, bar rendering stub

class WlRenderWindow final : public RenderWindow {
public:
    WlRenderWindow(wlr_scene_tree* /*root*/, const RenderWindowCreateInfo& info)
        : monitor_index_(info.monitor_index),
          x_(info.pos.x()), y_(info.pos.y()),
          w_(std::max(1, info.size.x())), h_(std::max(1, info.size.y())) {
        // TODO: implement via wl_shm pool when wlr_buffer_impl is opaque
        LOG_ERR("WlRenderWindow: bar rendering not implemented for wlroots 0.18+ yet");
    }

    ~WlRenderWindow() override = default;

    WindowId id() const override { return NO_WINDOW; }
    int monitor_index() const override { return monitor_index_; }
    int x() const override { return x_; }
    int y() const override { return y_; }
    int width() const override { return w_; }
    int height() const override { return h_; }

    cairo_t* cairo() override { return nullptr; }
    void present() override {}
    void set_visible(bool) override {}
    void raise() override {}
    void lower() override {}
    void move_to(int x, int y) override { x_ = x; y_ = y; }
    void reserve_top_strut(int, int, int) override {}
    void reserve_bottom_strut(int, int, int) override {}

private:
    int monitor_index_;
    int x_, y_, w_, h_;
};

#endif // WLR_BUFFER_IMPL_OPAQUE

// ---------------------------------------------------------------------------
// WlRenderPort
// ---------------------------------------------------------------------------
class WlRenderPort final : public RenderPort {
public:
    WlRenderPort(wlr_scene_tree* root, wlr_renderer* renderer)
        : root_(root), renderer_(renderer) {}

    std::unique_ptr<RenderWindow> create_window(const RenderWindowCreateInfo& info) override {
        return std::make_unique<WlRenderWindow>(root_, info);
    }

    uint32_t black_pixel() const override { return 0xFF000000u; }

private:
    wlr_scene_tree* root_;
    wlr_renderer*   renderer_;
};

std::unique_ptr<RenderPort> create_render_port(wlr_scene_tree* root, wlr_renderer* renderer) {
    return std::make_unique<WlRenderPort>(root, renderer);
}

} // namespace backend::wl
