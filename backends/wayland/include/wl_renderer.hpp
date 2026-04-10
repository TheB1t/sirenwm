#pragma once

// ---------------------------------------------------------------------------
// WlRenderer — RAII owner of wlr_renderer, wlr_allocator, wlr_compositor.
//
// These three form an inseparable unit:
//   renderer  = wlr_renderer_autocreate(backend)
//   allocator = wlr_allocator_autocreate(backend, renderer)
//   compositor is a Wayland global destroyed via wl_display_destroy
// ---------------------------------------------------------------------------

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
}

class WlRenderer {
    public:
        WlRenderer(wlr_backend* backend, wl_display* display);
        ~WlRenderer();

        WlRenderer(const WlRenderer&)            = delete;
        WlRenderer& operator=(const WlRenderer&) = delete;

        wlr_renderer*  renderer()   const noexcept { return renderer_; }
        wlr_allocator* allocator()  const noexcept { return allocator_; }
        bool           is_software() const noexcept { return software_; }

        // Must be called for each new output before first use.
        void init_output(wlr_output* output) noexcept;

    private:
        wlr_renderer*   renderer_   = nullptr;
        wlr_allocator*  allocator_  = nullptr;
        wlr_compositor* compositor_ = nullptr; // destroyed via wl_display_destroy
        bool software_              = false;
};
