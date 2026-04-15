#pragma once

#include <swm/ipc/channel.hpp>
#include <swm/ipc/backend_handlers.hpp>
#include <wl/server/runtime/display_state.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

namespace wl::server {

class BackendControlServer final : public swm::ipc::BackendEventHandler {
    public:
        BackendControlServer(const std::string& socket_path, DisplayState& state);
        ~BackendControlServer();

        BackendControlServer(const BackendControlServer&)            = delete;
        BackendControlServer& operator=(const BackendControlServer&) = delete;

        bool valid() const noexcept { return static_cast<bool>(listener_); }
        int  listener_fd() const noexcept { return listener_.fd(); }
        int  client_fd() const noexcept { return client_.fd(); }

        const std::string& socket_path() const noexcept { return socket_path_; }

        void accept_pending();
        void dispatch_pending();
        void disconnect_client();

        using swm::ipc::BackendEventHandler::on;

#define SWM_IPC_FORWARD_DISPLAY_EVENT(TypeName, MessageKind) \
    void on(const swm::ipc::TypeName& msg) override { (void)send_event(msg); }

        SWM_IPC_BACKEND_EVENT_MESSAGES(SWM_IPC_FORWARD_DISPLAY_EVENT)

#undef SWM_IPC_FORWARD_DISPLAY_EVENT

        // IPC dispatch entrypoints. These stay public so the shared libipc
        // dispatcher can invoke them without friend/template access hacks.
        bool on(const swm::ipc::Hello& msg);
        bool on(const swm::ipc::SnapshotRequest& msg);
        void on(const swm::ipc::ConfigureSurface& msg);
        void on(const swm::ipc::SetSurfaceVisible& msg);
        void on(const swm::ipc::SetSurfaceActivated& msg);
        void on(const swm::ipc::SetSurfaceStacking& msg);
        void on(const swm::ipc::CloseSurface& msg);
        void on(const swm::ipc::WarpPointer& msg);
        void on(const swm::ipc::SetKeyboardIntercepts& msg);
        bool on(const swm::ipc::GrabPointer&, int received_fd);
        bool on(const swm::ipc::UngrabPointer&, int received_fd);
        void on(const swm::ipc::SetSurfaceBorder& msg);
        void on(const swm::ipc::CreateOverlay& msg);
        bool on(const swm::ipc::UpdateOverlay& msg, int received_fd);
        void on(const swm::ipc::DestroyOverlay& msg);

    private:
        bool dispatch_message(const swm::ipc::MessageHeader& header,
            const void* payload, int received_fd);
        bool dispatch_one_message();
        bool send_snapshot();

        template <typename Payload>
        bool send_event(const Payload& payload) {
            if (!client_)
                return true;
            if (client_.send_message(payload))
                return true;
            std::fprintf(stderr,
                "sirenwm-wayland(display-server): control send failed (type=%u): %s\n",
                static_cast<unsigned>(swm::ipc::MessageTraits<Payload>::type),
                std::strerror(errno));
            disconnect_client();
            return false;
        }

        swm::ipc::Channel listener_;
        swm::ipc::Channel client_;
        std::string       socket_path_;
        DisplayState&     state_;
        uint32_t          snapshot_generation_ = 0;
};

} // namespace wl::server
