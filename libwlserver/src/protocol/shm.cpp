#include <wl/server/protocol/shm.hpp>

#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

extern "C" {
#include <wayland-server-protocol.h>
}

namespace wl::server {

template <typename T>
concept HasShmRelease = requires(T v) {
    v.release;
};

template <typename T>
static void maybe_set_release(T& impl) {
    if constexpr (HasShmRelease<T>) {
        impl.release = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            };
    }
}

struct Shm::ShmPool {
    int     fd   = -1;
    void*   data = nullptr;
    int32_t size = 0;

    ShmPool() = default;
    ~ShmPool() {
        if (data && data != MAP_FAILED) munmap(data, static_cast<size_t>(size));
        if (fd >= 0) close(fd);
    }

    ShmPool(const ShmPool&)            = delete;
    ShmPool& operator=(const ShmPool&) = delete;
};

struct Shm::ShmBuffer {
    ShmPool* pool   = nullptr;
    int32_t  offset = 0;
    int32_t  width  = 0;
    int32_t  height = 0;
    int32_t  stride = 0;
    uint32_t format = 0;

    void* data() const {
        if (!pool || !pool->data || pool->data == MAP_FAILED) return nullptr;
        return static_cast<char*>(pool->data) + offset;
    }
};

static const struct wl_buffer_interface* buffer_vtable() {
    static const struct wl_buffer_interface impl = {
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
    };
    return &impl;
}

const wl_interface* Shm::interface() { return &wl_shm_interface; }

Shm::Shm(Display& display) : global_(display, this) {}
Shm::~Shm() = default;

BufferView Shm::buffer_view(wl_resource* buffer_resource) const {
    if (!buffer_resource)
        return {};

    if (!wl_resource_instance_of(buffer_resource, &wl_buffer_interface, buffer_vtable()))
        return {};

    auto* buffer = static_cast<const ShmBuffer*>(wl_resource_get_user_data(buffer_resource));
    if (!buffer)
        return {};

    void* data = buffer->data();
    if (!data)
        return {};

    return BufferView{
        .data   = data,
        .width  = buffer->width,
        .height = buffer->height,
        .stride = buffer->stride,
        .format = buffer->format,
    };
}

void Shm::bind(wl_client* client, uint32_t version, uint32_t id) {
    auto* resource = wl_resource_create(client, &wl_shm_interface,
            static_cast<int>(version), id);
    if (!resource) {
        wl_client_post_no_memory(client); return;
    }

    static const struct wl_shm_interface vtable = [] {
            struct wl_shm_interface impl {};
            impl.create_pool = [](wl_client* c, wl_resource* r, uint32_t id, int32_t fd, int32_t sz) {
                    auto* self = static_cast<Shm*>(wl_resource_get_user_data(r));
                    self->create_pool(c, r, id, fd, sz);
                };
            maybe_set_release(impl);
            return impl;
        }();
    wl_resource_set_implementation(resource, &vtable, this, nullptr);

    wl_shm_send_format(resource, WL_SHM_FORMAT_ARGB8888);
    wl_shm_send_format(resource, WL_SHM_FORMAT_XRGB8888);
}

void Shm::create_pool(wl_client* client, wl_resource* resource,
    uint32_t id, int32_t fd, int32_t size) {
    auto pool = std::make_unique<ShmPool>();
    pool->fd   = fd;
    pool->size = size;
    pool->data = mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_SHARED, fd, 0);
    if (pool->data == MAP_FAILED) {
        wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "failed to mmap shm pool");
        close(fd);
        return;
    }

    auto* pool_res = wl_resource_create(client, &wl_shm_pool_interface, 1, id);
    if (!pool_res) {
        wl_client_post_no_memory(client); return;
    }

    static const struct wl_shm_pool_interface pool_vtable = {
        .create_buffer = [](wl_client* client, wl_resource* resource,
            uint32_t id, int32_t offset,
            int32_t width, int32_t height,
            int32_t stride, uint32_t format) {
                auto* pool = static_cast<ShmPool*>(wl_resource_get_user_data(resource));
                if (offset < 0 || width <= 0 || height <= 0 || stride <= 0 ||
                    offset + static_cast<int64_t>(height) * stride > pool->size) {
                    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE, "invalid buffer params");
                    return;
                }

                auto buf    = std::make_unique<ShmBuffer>();
                buf->pool   = pool;
                buf->offset = offset;
                buf->width  = width;
                buf->height = height;
                buf->stride = stride;
                buf->format = format;

                auto* buf_res = wl_resource_create(client, &wl_buffer_interface, 1, id);
                if (!buf_res) {
                    wl_client_post_no_memory(client); return;
                }

                wl_resource_set_implementation(buf_res, buffer_vtable(), buf.get(),
                    [](wl_resource* r) {
                        auto* b = static_cast<ShmBuffer*>(wl_resource_get_user_data(r));
                        if (b)
                            delete b;
                    });
                (void)buf.release();
            },
        .destroy = [](wl_client*, wl_resource* r) {
                wl_resource_destroy(r);
            },
        .resize = [](wl_client*, wl_resource* resource, int32_t size) {
                auto* pool = static_cast<ShmPool*>(wl_resource_get_user_data(resource));
                if (size <= pool->size) return;
                void* new_data = mremap(pool->data, static_cast<size_t>(pool->size),
                        static_cast<size_t>(size), MREMAP_MAYMOVE);
                if (new_data == MAP_FAILED) return;
                pool->data = new_data;
                pool->size = size;
            },
    };

    auto*                                     pool_ptr = pool.get();
    pools_.push_back(std::move(pool));
    wl_resource_set_implementation(pool_res, &pool_vtable, pool_ptr, nullptr);
}

} // namespace wl::server
