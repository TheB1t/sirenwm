#include <backend/render_port.hpp>
#include <wl_cpu_buffer.hpp>
#include <log.hpp>

#include <algorithm>
#include <memory>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <wlr/types/wlr_scene.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/render/drm_format_set.h>
#include <drm_fourcc.h>
}

// ---------------------------------------------------------------------------
// WlRenderWindow — internal compositor surface for bar/overlay drawing.
//
// Two implementation paths selected at compile time:
//   !WLR_BUFFER_IMPL_OPAQUE  — custom wlr_buffer via WlCpuBuffer (wlroots <0.18)
//   WLR_BUFFER_IMPL_OPAQUE   — wlr_allocator_create_buffer + data_ptr_access (0.18+)
//
// The 0.18+ path allocates a buffer via the compositor's allocator (which
// uses wl_shm / pixman under a software renderer), locks it for CPU access
// on each present(), copies the Cairo pixels in, then hands the buffer to
// wlr_scene_buffer_set_buffer.
// ---------------------------------------------------------------------------

namespace backend::wl {

#ifndef WLR_BUFFER_IMPL_OPAQUE

class WlRenderWindow final : public RenderWindow {
    public:
        WlRenderWindow(wlr_scene_tree* root, const RenderWindowCreateInfo& info,
            wlr_renderer* /*renderer*/, wlr_allocator* /*allocator*/)
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

    private:
        int monitor_index_;
        int x_, y_, w_, h_;
        WlCpuBuffer*      buf_  = nullptr;
        wlr_scene_tree*   tree_ = nullptr;
        wlr_scene_buffer* sbuf_ = nullptr;
};

#else // WLR_BUFFER_IMPL_OPAQUE — wlroots 0.18+

// wlroots 0.18+: wlr_buffer_impl is opaque, so we cannot implement a custom
// wlr_buffer.  Instead we use the compositor's wlr_allocator to create a
// native buffer (wl_shm-backed under pixman/X11 renderer), lock it for CPU
// access, copy Cairo pixels in, and hand it to wlr_scene_buffer_set_buffer.

class WlRenderWindow final : public RenderWindow {
    public:
        WlRenderWindow(wlr_scene_tree* root, const RenderWindowCreateInfo& info,
            wlr_renderer* /*renderer*/, wlr_allocator* allocator)
            : monitor_index_(info.monitor_index),
              x_(info.pos.x()), y_(info.pos.y()),
              w_(std::max(1, info.size.x())),
              h_(std::max(1, info.size.y())),
              allocator_(allocator) {

            // Cairo draws into our own pixel array.
            stride_ = w_ * 4;
            pixels_ = static_cast<uint8_t*>(std::calloc(1, (size_t)(stride_ * h_)));
            if (!pixels_) {
                LOG_ERR("WlRenderWindow: pixel alloc failed");
                return;
            }

            cairo_surface_ = cairo_image_surface_create_for_data(
                pixels_, CAIRO_FORMAT_ARGB32, w_, h_, stride_);
            if (cairo_surface_status(cairo_surface_) != CAIRO_STATUS_SUCCESS) {
                LOG_ERR("WlRenderWindow: cairo surface create failed");
                std::free(pixels_); pixels_ = nullptr;
                return;
            }
            cairo_ctx_ = cairo_create(cairo_surface_);

            tree_ = wlr_scene_tree_create(root);
            sbuf_ = wlr_scene_buffer_create(tree_, nullptr);
            wlr_scene_buffer_set_dest_size(sbuf_, w_, h_);
            wlr_scene_node_set_position(&tree_->node, x_, y_);

            // Pre-allocate the native buffer via the compositor's allocator.
            // Under WLR_BACKENDS=x11 the allocator creates a wl_shm buffer.
            allocate_wlr_buffer();
        }

        ~WlRenderWindow() override {
            if (wlr_buf_) wlr_buffer_drop(wlr_buf_);
            if (sbuf_)    wlr_scene_node_destroy(&sbuf_->node);
            if (tree_)    wlr_scene_node_destroy(&tree_->node);
            if (cairo_ctx_)     cairo_destroy(cairo_ctx_);
            if (cairo_surface_) cairo_surface_destroy(cairo_surface_);
            std::free(pixels_);
        }

        WindowId id() const override { return NO_WINDOW; }
        int monitor_index() const override { return monitor_index_; }
        int x() const override { return x_; }
        int y() const override { return y_; }
        int width() const override { return w_; }
        int height() const override { return h_; }

        cairo_t* cairo() override { return cairo_ctx_; }

        void present() override {
            if (!sbuf_ || !pixels_) return;
            cairo_surface_flush(cairo_surface_);

            if (!wlr_buf_ && !allocate_wlr_buffer()) return;

            // Lock the native buffer for CPU write access.
            void*    dst_data   = nullptr;
            uint32_t dst_fmt    = 0;
            size_t   dst_stride = 0;
            if (!wlr_buffer_begin_data_ptr_access(wlr_buf_,
                WLR_BUFFER_DATA_PTR_ACCESS_WRITE,
                &dst_data, &dst_fmt, &dst_stride)) {
                LOG_ERR("WlRenderWindow: wlr_buffer_begin_data_ptr_access failed");
                return;
            }

            // Copy Cairo pixels into the native buffer row by row.
            const int copy_stride = std::min((int)dst_stride, stride_);
            for (int row = 0; row < h_; ++row) {
                std::memcpy(
                    static_cast<uint8_t*>(dst_data) + row * (int)dst_stride,
                    pixels_ + row * stride_,
                    (size_t)copy_stride);
            }

            wlr_buffer_end_data_ptr_access(wlr_buf_);

            // Hand the buffer to the scene; scene keeps a reference.
            wlr_scene_buffer_set_buffer(sbuf_, wlr_buf_);
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

    private:
        bool allocate_wlr_buffer() {
            if (!allocator_) return false;

            // Request ARGB8888 with no modifiers (implicit linear).
            // pixman/shm allocator always supports this format.
            wlr_drm_format fmt{};
            fmt.format    = DRM_FORMAT_ARGB8888;
            fmt.len       = 0;
            fmt.capacity  = 0;
            fmt.modifiers = nullptr;

            wlr_buf_ = wlr_allocator_create_buffer(allocator_, w_, h_, &fmt);
            if (!wlr_buf_) {
                LOG_ERR("WlRenderWindow: wlr_allocator_create_buffer(%dx%d) failed", w_, h_);
                return false;
            }
            return true;
        }

        int      monitor_index_;
        int      x_, y_, w_, h_;
        int      stride_                 = 0;
        uint8_t* pixels_                 = nullptr;
        cairo_surface_t*  cairo_surface_ = nullptr;
        cairo_t*          cairo_ctx_     = nullptr;
        wlr_allocator*    allocator_     = nullptr;
        wlr_buffer*       wlr_buf_       = nullptr;
        wlr_scene_tree*   tree_          = nullptr;
        wlr_scene_buffer* sbuf_          = nullptr;
};

#endif // WLR_BUFFER_IMPL_OPAQUE

// ---------------------------------------------------------------------------
// WlRenderPort
// ---------------------------------------------------------------------------
class WlRenderPort final : public RenderPort {
    public:
        WlRenderPort(wlr_scene_tree* root, wlr_renderer* renderer, wlr_allocator* allocator)
            : root_(root), renderer_(renderer), allocator_(allocator) {}

        std::unique_ptr<RenderWindow> create_window(const RenderWindowCreateInfo& info) override {
            return std::make_unique<WlRenderWindow>(root_, info, renderer_, allocator_);
        }

        uint32_t black_pixel() const override { return 0xFF000000u; }

    private:
        wlr_scene_tree* root_;
        wlr_renderer*   renderer_;
        wlr_allocator*  allocator_;
};

std::unique_ptr<RenderPort> create_render_port(wlr_scene_tree* root, wlr_renderer* renderer,
    wlr_allocator* allocator) {
    return std::make_unique<WlRenderPort>(root, renderer, allocator);
}

} // namespace backend::wl
