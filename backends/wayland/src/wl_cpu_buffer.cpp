#include <wl_cpu_buffer.hpp>
#include <log.hpp>

#include <cstdlib>
#include <cstring>

extern "C" {
#include <drm_fourcc.h>
}

// ---------------------------------------------------------------------------
// wlr_buffer_impl — only available in wlroots < 0.18
// ---------------------------------------------------------------------------

#ifndef WLR_BUFFER_IMPL_OPAQUE

const wlr_buffer_impl WlCpuBuffer::impl = {
    .destroy               = WlCpuBuffer::destroy_impl,
    .get_dmabuf            = nullptr,
    .get_shm               = nullptr,
    .begin_data_ptr_access = WlCpuBuffer::begin_data_ptr,
    .end_data_ptr_access   = WlCpuBuffer::end_data_ptr,
};

void WlCpuBuffer::destroy_impl(wlr_buffer* b) {
    auto* self = reinterpret_cast<WlCpuBuffer*>(b);
    if (self->cairo_ctx)     cairo_destroy(self->cairo_ctx);
    if (self->cairo_surface) cairo_surface_destroy(self->cairo_surface);
    std::free(self->pixels);
    delete self;
}

bool WlCpuBuffer::begin_data_ptr(wlr_buffer* b, uint32_t /*flags*/,
    void** data, uint32_t* fmt, size_t* stride) {
    auto* self = reinterpret_cast<WlCpuBuffer*>(b);
    *data   = self->pixels;
    *fmt    = DRM_FORMAT_ARGB8888;
    *stride = (size_t)self->stride;
    return true;
}

void WlCpuBuffer::end_data_ptr(wlr_buffer* /*b*/) {}

#endif // !WLR_BUFFER_IMPL_OPAQUE

// ---------------------------------------------------------------------------
// create / destroy
// ---------------------------------------------------------------------------

WlCpuBuffer* WlCpuBuffer::create(int w, int h) {
    if (w <= 0 || h <= 0)
        return nullptr;

    auto* buf = new WlCpuBuffer();
    buf->width  = w;
    buf->height = h;
    buf->stride = w * 4;
    buf->pixels = static_cast<uint8_t*>(std::calloc(1, (size_t)(w * h * 4)));
    if (!buf->pixels) {
        delete buf;
        return nullptr;
    }

#ifndef WLR_BUFFER_IMPL_OPAQUE
    wlr_buffer_init(&buf->base, &WlCpuBuffer::impl, w, h);
#endif

    buf->cairo_surface = cairo_image_surface_create_for_data(
        buf->pixels, CAIRO_FORMAT_ARGB32, w, h, buf->stride);
    if (cairo_surface_status(buf->cairo_surface) != CAIRO_STATUS_SUCCESS) {
        LOG_ERR("WlCpuBuffer: cairo_image_surface_create failed");
        std::free(buf->pixels);
        delete buf;
        return nullptr;
    }
    buf->cairo_ctx = cairo_create(buf->cairo_surface);

    return buf;
}

void WlCpuBuffer::destroy(WlCpuBuffer* buf) {
    if (!buf) return;
    if (buf->cairo_ctx)     cairo_destroy(buf->cairo_ctx);
    if (buf->cairo_surface) cairo_surface_destroy(buf->cairo_surface);
    std::free(buf->pixels);
    delete buf;
}
