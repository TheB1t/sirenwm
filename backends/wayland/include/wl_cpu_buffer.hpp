#pragma once

// ---------------------------------------------------------------------------
// WlCpuBuffer — wlr_buffer backed by a heap-allocated pixel array.
//
// Provides a cairo_t* for drawing; on present(), the scene system is notified
// that the buffer has changed so the next output commit picks it up.
//
// Format: WL_SHM_FORMAT_ARGB8888 (Cairo ARGB32 layout, same byte order).
// ---------------------------------------------------------------------------

#include <cairo/cairo.h>
#include <memory>
#include <cstring>

extern "C" {
#include <wlr/types/wlr_buffer.h>
#include <drm_fourcc.h>
}

struct WlCpuBuffer {
    wlr_buffer base;   // must be first — wlr_buffer_impl callbacks use container_of

    int            width  = 0;
    int            height = 0;
    int            stride = 0;
    uint8_t*       pixels = nullptr;   // width * height * 4 bytes, ARGB8888

    cairo_surface_t* cairo_surface = nullptr;
    cairo_t*         cairo_ctx     = nullptr;

    static WlCpuBuffer* create(int w, int h);
    static void         destroy_impl(wlr_buffer* buf);

    // wlr_buffer_impl: data pointer access (CPU readback path used by renderer)
    static bool begin_data_ptr(wlr_buffer* buf, uint32_t flags, void** data,
                               uint32_t* fmt, size_t* stride);
    static void end_data_ptr(wlr_buffer* buf);

    static const wlr_buffer_impl impl;

private:
    WlCpuBuffer() = default;
};
