#pragma once

// ---------------------------------------------------------------------------
// WlCpuBuffer — software raster buffer for bar/overlay drawing.
//
// Two implementations depending on wlroots version:
//   - Pre-0.18: wlr_buffer_impl is public → we implement a custom wlr_buffer.
//   - 0.18+:    wlr_buffer_impl is opaque → we use a heap pixel array only
//               and upload via wlr_scene_buffer's set_buffer_with_damage path.
// ---------------------------------------------------------------------------

#include <cairo/cairo.h>
#include <cstring>

extern "C" {
#include <wlr/types/wlr_buffer.h>
#include <drm_fourcc.h>
}

struct WlCpuBuffer {
    int   width  = 0;
    int   height = 0;
    int   stride = 0;

    uint8_t* pixels = nullptr;   // heap-allocated, width*height*4 ARGB8888

    cairo_surface_t* cairo_surface = nullptr;
    cairo_t*         cairo_ctx     = nullptr;

#ifndef WLR_BUFFER_IMPL_OPAQUE
    // In wlroots <0.18 the wlr_buffer is embedded.
    wlr_buffer base;   // must be first for container_of in impl callbacks

    static const wlr_buffer_impl impl;
    static void  destroy_impl(wlr_buffer* buf);
    static bool  begin_data_ptr(wlr_buffer* buf, uint32_t flags,
                                void** data, uint32_t* fmt, size_t* stride);
    static void  end_data_ptr(wlr_buffer* buf);
#endif

    // Create a CPU buffer of the given dimensions.
    static WlCpuBuffer* create(int w, int h);

    // Destroy (free all resources). Also calls wlr_buffer_drop on <0.18.
    static void destroy(WlCpuBuffer* buf);

private:
    WlCpuBuffer() = default;
};
