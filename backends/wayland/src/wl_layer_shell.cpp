#ifndef SIRENWM_NO_LAYER_SHELL

#include <wl_layer_shell.hpp>
#include <log.hpp>

WlLayerShell::WlLayerShell(wl_display* display, uint32_t version, SurfaceCb on_new_surface) {
    shell_ = wlr_layer_shell_v1_create(display, version);
    if (!shell_)
        LOG_ERR("WlLayerShell: wlr_layer_shell_v1_create failed");

    on_new_surface_.connect(&shell_->events.new_surface,
        [cb = std::move(on_new_surface)](wlr_layer_surface_v1* s) { cb(s); });
}

#endif // SIRENWM_NO_LAYER_SHELL
