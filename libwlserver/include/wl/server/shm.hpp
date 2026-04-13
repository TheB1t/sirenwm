#pragma once

#include <wl/display.hpp>
#include <wl/global.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace wl::server {

struct BufferView {
    void*    data   = nullptr;
    int32_t  width  = 0;
    int32_t  height = 0;
    int32_t  stride = 0;
    uint32_t format = 0;
};

class Shm {
public:
    explicit Shm(Display& display);
    ~Shm();

    Shm(const Shm&)            = delete;
    Shm& operator=(const Shm&) = delete;

    static const wl_interface* interface();
    static int version() { return 1; }

    void bind(wl_client* client, uint32_t version, uint32_t id);
    BufferView buffer_view(wl_resource* buffer_resource) const;

private:
    struct ShmPool;
    struct ShmBuffer;

    wl::Global<Shm> global_;
    std::vector<std::unique_ptr<ShmPool>> pools_;

    void create_pool(wl_client* client, wl_resource* resource,
                     uint32_t id, int32_t fd, int32_t size);

    friend class Compositor;
};

} // namespace wl::server
