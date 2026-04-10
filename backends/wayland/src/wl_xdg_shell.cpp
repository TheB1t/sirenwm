#include <wl_xdg_shell.hpp>
#include <log.hpp>

WlXdgShell::WlXdgShell(wl_display* display, uint32_t version, SurfaceCb on_new_surface) {
    shell_ = wlr_xdg_shell_create(display, version);
    if (!shell_)
        LOG_ERR("WlXdgShell: wlr_xdg_shell_create failed");

    on_new_surface_.connect(&shell_->events.new_surface,
        [cb = std::move(on_new_surface)](wlr_xdg_surface* s) { cb(s); });
}
