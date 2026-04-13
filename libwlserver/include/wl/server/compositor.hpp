#pragma once

#include <wl/display.hpp>
#include <wl/global.hpp>
#include <wl/server/shm.hpp>
#include <wl/server/surface_id.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace wl::server {

struct SurfaceInfo {
    SurfaceId id;
    bool      has_commit  = false;
    bool      has_buffer  = false;
    int32_t   buf_width   = 0;
    int32_t   buf_height  = 0;
};

class Seat;

class Compositor {
public:
    class SurfaceCommitSubscription {
    public:
        SurfaceCommitSubscription() = default;
        ~SurfaceCommitSubscription();

        SurfaceCommitSubscription(const SurfaceCommitSubscription&)            = delete;
        SurfaceCommitSubscription& operator=(const SurfaceCommitSubscription&) = delete;
        SurfaceCommitSubscription(SurfaceCommitSubscription&& other) noexcept;
        SurfaceCommitSubscription& operator=(SurfaceCommitSubscription&& other) noexcept;

    private:
        friend class Compositor;
        SurfaceCommitSubscription(Compositor* owner, uint64_t id) noexcept
            : owner_(owner), id_(id) {}

        void reset() noexcept;

        Compositor* owner_ = nullptr;
        uint64_t    id_    = 0;
    };

    using SurfaceCommitCallback = std::function<void(SurfaceId)>;

    Compositor(Display& display, Shm& shm);

    Compositor(const Compositor&)            = delete;
    Compositor& operator=(const Compositor&) = delete;

    const SurfaceInfo* surface_info(SurfaceId id) const;
    BufferView         buffer_view(SurfaceId id);
    SurfaceId          id_from_resource(wl_resource* resource) const;
    SurfaceCommitSubscription subscribe_surface_commit(SurfaceCommitCallback cb);

    static const wl_interface* interface();
    static int version() { return 5; }
    void bind(wl_client* client, uint32_t version, uint32_t id);

private:
    friend class Seat;

    struct WlSurface {
        SurfaceId    id;
        wl_resource* resource       = nullptr;
        wl_resource* buffer         = nullptr;
        bool         has_commit     = false;
        int32_t      buf_width      = 0;
        int32_t      buf_height     = 0;
        wl_resource* pending_buffer = nullptr;
        bool         pending_attach = false;
        wl_listener  buffer_destroy_listener = {};

        void clear_buffer();
        void set_buffer(wl_resource* buf);
    };

    wl::Global<Compositor> global_;
    Shm& shm_;
    uint32_t next_surface_id_ = 1;
    std::unordered_map<wl_resource*, WlSurface> surfaces_;
    std::unordered_map<uint32_t, wl_resource*>  id_to_resource_;
    uint64_t next_surface_commit_subscription_id_ = 1;

    struct SurfaceCommitListener {
        uint64_t              id = 0;
        SurfaceCommitCallback callback;
    };
    std::vector<SurfaceCommitListener> surface_commit_listeners_;

    WlSurface* surface_from_resource(wl_resource* resource);
    wl_resource* resource_for(SurfaceId id) const;
    void unsubscribe_surface_commit(uint64_t id);

    void create_surface(wl_client* client, uint32_t id, int version);
    void commit(wl_resource* resource);

    static const void* compositor_vtable();
    static const void* surface_vtable();
    static const void* region_vtable();
};

class Subcompositor {
public:
    explicit Subcompositor(Display& display);

    Subcompositor(const Subcompositor&)            = delete;
    Subcompositor& operator=(const Subcompositor&) = delete;

    static const wl_interface* interface();
    static int version() { return 1; }

    void bind(wl_client* client, uint32_t version, uint32_t id);

private:
    wl::Global<Subcompositor> global_;
};

} // namespace wl::server
