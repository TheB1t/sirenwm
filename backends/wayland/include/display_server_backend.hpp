#pragma once

#include <backend/backend.hpp>

#include <swm/ipc/channel.hpp>
#include <display_server_ports.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

struct DisplayServerSurfaceInfo {
    uint32_t    id = 0;
    std::string app_id;
    std::string title;
    uint32_t    pid     = 0;
    bool        mapped  = false;
    bool        managed = false;
    int32_t     x       = 0;
    int32_t     y       = 0;
    int32_t     width   = 0;
    int32_t     height  = 0;
};

struct DisplayServerOutputInfo {
    uint32_t    id = 0;
    std::string name;
    int32_t     x       = 0;
    int32_t     y       = 0;
    int32_t     w       = 0;
    int32_t     h       = 0;
    int32_t     refresh = 0;
};

class DisplayServerBackend final : public Backend {
    public:
        DisplayServerBackend(Core& core, Runtime& runtime);
        ~DisplayServerBackend() override;

        int                   event_fd() const override;
        void                  pump_events(std::size_t max_events_per_tick) override;
        void                  render_frame() override;
        void on_reload_applied() override { reload_border_colors(); }
        void                  shutdown() override;
        void                  on_start(Core& core) override;

        backend::BackendPorts ports() override;

        const std::unordered_map<uint32_t, DisplayServerSurfaceInfo>& surfaces() const { return surfaces_; }
        const std::unordered_map<uint32_t, DisplayServerOutputInfo>&  outputs()  const { return outputs_; }

        void configure_surface(WindowId id, int32_t x, int32_t y, int32_t w, int32_t h);
        void set_surface_activated(WindowId id, uint32_t activated);
        void set_surface_visible(WindowId id, uint32_t visible);
        void set_surface_stacking(WindowId id, uint32_t stacking);
        void close_surface(WindowId id);
        void set_keyboard_intercepts(const void* data, std::size_t size_bytes);
        void warp_pointer(int32_t x, int32_t y);
        void grab_pointer();
        void ungrab_pointer();
        void set_surface_border(WindowId id, uint32_t width, uint32_t color);
        void create_overlay(uint32_t overlay_id, int32_t x, int32_t y, int32_t w, int32_t h);
        void update_overlay(uint32_t overlay_id, int fd, uint32_t bytes);
        void destroy_overlay(uint32_t overlay_id);

        // IPC dispatch entrypoints. These stay public so the shared libipc
        // dispatcher can invoke them without friend/template access hacks.
        bool on(const swm::ipc::Hello& msg);
        void on(const swm::ipc::SnapshotBegin& msg);
        void on(const swm::ipc::SnapshotEnd& msg);
        void on(const swm::ipc::OutputAdded& msg);
        void on(const swm::ipc::OutputRemoved& msg);
        void on(const swm::ipc::SurfaceCreated& msg);
        void on(const swm::ipc::SurfaceMapped& msg);
        void on(const swm::ipc::SurfaceUnmapped& msg);
        void on(const swm::ipc::SurfaceDestroyed& msg);
        void on(const swm::ipc::SurfaceCommitted& msg);
        void on(const swm::ipc::SurfaceTitleChanged& msg);
        void on(const swm::ipc::SurfaceAppIdChanged& msg);
        void on(const swm::ipc::Key& msg);
        void on(const swm::ipc::Button& msg);
        void on(const swm::ipc::PointerMotion& msg);
        void on(const swm::ipc::PointerEnter& msg);
        void on(const swm::ipc::OverlayExpose& msg);
        void on(const swm::ipc::OverlayButton& msg);

    private:
        swm::ipc::Channel control_;

        std::unordered_map<uint32_t, DisplayServerSurfaceInfo> surfaces_;
        std::unordered_map<uint32_t, DisplayServerOutputInfo>  outputs_;

        backend::DisplayServerInputPort    input_;
        backend::DisplayServerMonitorPort  monitor_;
        backend::DisplayServerRenderPort   render_;
        backend::DisplayServerKeyboardPort keyboard_;

        Runtime& runtime_;

        WindowId prev_focused_      = NO_WINDOW;
        uint32_t focused_border_    = 0xFF4488CC;
        uint32_t unfocused_border_  = 0xFF333333;
        bool     started_           = false;
        bool     snapshot_complete_ = false;

        void manage_surface(DisplayServerSurfaceInfo& s);
        void reload_border_colors();
        void bootstrap_snapshot();
        bool dispatch_control_message(const swm::ipc::MessageHeader& header,
            const void* payload, int received_fd);

        template <typename Payload>
        void send_control(const Payload& payload, int fd = -1) {
            if (!control_.send_message(payload, fd)) {
                throw std::runtime_error(
                    "display_server_backend: control IPC send failed (msg=" +
                    std::to_string(static_cast<uint16_t>(swm::ipc::MessageTraits<Payload>::type)) + ")");
            }
        }
};
