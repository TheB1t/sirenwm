#include <wl_ports.hpp>
#include <wl_backend.hpp>

#include <wl/shm_buffer.hpp>

#include <cairo/cairo.h>

#include <memory>

namespace backend {

WlRenderWindow::WlRenderWindow(const RenderWindowCreateInfo& info, WindowId id, WlBackend& backend)
    : backend_(backend)
      , id_(id)
      , overlay_id_(static_cast<uint32_t>(id))
      , monitor_index_(info.monitor_index)
      , x_(info.pos.x()), y_(info.pos.y())
      , w_(info.size.x()), h_(info.size.y()) {
    surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w_, h_);
    cr_      = cairo_create(surface_);

    backend_.create_overlay(overlay_id_, x_, y_, w_, h_);
    created_ = true;
}

WlRenderWindow::~WlRenderWindow() {
    if (cr_) cairo_destroy(cr_);
    if (surface_) cairo_surface_destroy(surface_);
    if (created_)
        backend_.destroy_overlay(overlay_id_);
}

void WlRenderWindow::present() {
    cairo_surface_flush(surface_);

    auto* data = cairo_image_surface_get_data(surface_);
    if (!data) return;

    size_t        size = static_cast<size_t>(w_) * h_ * 4;
    wl::ShmBuffer shm(size);
    if (!shm) return;

    shm.write(data, size);
    backend_.update_overlay(overlay_id_, shm.release_fd(), static_cast<uint32_t>(size));
    backend_.display().flush();
}

void WlRenderWindow::set_visible(bool) {}
void WlRenderWindow::raise() {}
void WlRenderWindow::lower() {}

std::unique_ptr<RenderWindow> WlRenderPort::create_window(const RenderWindowCreateInfo& info) {
    return std::make_unique<WlRenderWindow>(info, static_cast<WindowId>(next_id_++), backend_);
}

} // namespace backend
