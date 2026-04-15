#include <display_server_ports.hpp>
#include <display_server_backend.hpp>

#include <support/log.hpp>
#include <swm/ipc/shared_buffer.hpp>

#include <cairo/cairo.h>

#include <exception>
#include <memory>

namespace backend {

DisplayServerRenderWindow::DisplayServerRenderWindow(const RenderWindowCreateInfo& info, WindowId id, DisplayServerBackend& backend)
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

DisplayServerRenderWindow::~DisplayServerRenderWindow() {
    if (cr_) cairo_destroy(cr_);
    if (surface_) cairo_surface_destroy(surface_);
    if (created_) {
        try {
            backend_.destroy_overlay(overlay_id_);
        } catch (const std::exception& e) {
            LOG_ERR("DisplayServerRenderWindow: destroy_overlay(%u) failed: %s", overlay_id_, e.what());
        } catch (...) {
            LOG_ERR("DisplayServerRenderWindow: destroy_overlay(%u) failed with non-standard exception", overlay_id_);
        }
    }
}

void DisplayServerRenderWindow::present() {
    cairo_surface_flush(surface_);

    auto* data = cairo_image_surface_get_data(surface_);
    if (!data) return;

    size_t                 size = static_cast<size_t>(w_) * h_ * 4;
    swm::ipc::SharedBuffer shm(size);
    if (!shm) return;

    shm.write(data, size);
    backend_.update_overlay(overlay_id_, shm.release_fd(), static_cast<uint32_t>(size));
}

void DisplayServerRenderWindow::set_visible(bool) {}
void DisplayServerRenderWindow::raise() {}
void DisplayServerRenderWindow::lower() {}

std::unique_ptr<RenderWindow> DisplayServerRenderPort::create_window(const RenderWindowCreateInfo& info) {
    return std::make_unique<DisplayServerRenderWindow>(info, static_cast<WindowId>(next_id_++), backend_);
}

} // namespace backend
