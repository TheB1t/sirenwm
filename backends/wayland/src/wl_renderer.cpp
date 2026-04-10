#include <wl_renderer.hpp>
#include <wl_compat.hpp>
#include <log.hpp>

WlRenderer::WlRenderer(wlr_backend* backend, wl_display* display) {
    renderer_ = wlr_renderer_autocreate(backend);
    if (!renderer_)
        LOG_ERR("WlRenderer: wlr_renderer_autocreate failed");

    wlr_renderer_init_wl_display(renderer_, display);

    allocator_ = wlr_allocator_autocreate(backend, renderer_);
    if (!allocator_)
        LOG_ERR("WlRenderer: wlr_allocator_autocreate failed");

    compositor_ = wlr_compositor_create(display, 6, renderer_);

    software_ = wlr_compat::is_software_renderer(renderer_);
}

WlRenderer::~WlRenderer() {
    // compositor_ is destroyed via wl_display_destroy
    if (allocator_)
        wlr_allocator_destroy(allocator_);
    if (renderer_)
        wlr_renderer_destroy(renderer_);
}

void WlRenderer::init_output(wlr_output* output) noexcept {
    wlr_output_init_render(output, allocator_, renderer_);
}
