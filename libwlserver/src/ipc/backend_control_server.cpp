#include <wl/server/ipc/backend_control_server.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <swm/ipc/message_dispatch.hpp>

#include <poll.h>
#include <unistd.h>

namespace wl::server {

namespace {

bool poll_readable(int fd, int timeout_ms) {
    if (fd < 0)
        return false;

    pollfd pfd {
        .fd = fd,
        .events = POLLIN,
        .revents = 0,
    };
    const int rc = ::poll(&pfd, 1, timeout_ms);
    return rc > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

} // namespace

BackendControlServer::BackendControlServer(const std::string& socket_path, DisplayState& state)
    : listener_(swm::ipc::Channel::listen_seqpacket(socket_path))
    , socket_path_(socket_path)
    , state_(state) {
    if (listener_)
        listener_.set_nonblocking(true);
    state_.set_event_handler(this);
}

BackendControlServer::~BackendControlServer() {
    state_.set_event_handler(nullptr);
    disconnect_client();
    listener_.close();
    if (!socket_path_.empty())
        ::unlink(socket_path_.c_str());
}

void BackendControlServer::accept_pending() {
    if (!listener_)
        return;

    while (true) {
        auto client = listener_.accept();
        if (!client) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return;
        }

        client.set_nonblocking(true);

        if (client_) {
            std::fprintf(stderr, "sirenwm-wayland(display-server): replacing control client\n");
            disconnect_client();
        } else {
            std::fprintf(stderr, "sirenwm-wayland(display-server): control client connected\n");
        }
        client_ = std::move(client);
    }
}

void BackendControlServer::disconnect_client() {
    if (!client_)
        return;

    state_.set_keyboard_intercepts(std::span<const swm::ipc::KeyIntercept> {});
    state_.ungrab_pointer();
    client_.close();
    std::fprintf(stderr, "sirenwm-wayland(display-server): control client disconnected\n");
}

void BackendControlServer::dispatch_pending() {
    if (!client_)
        return;

    do {
        if (!dispatch_one_message())
            break;
    } while (client_ && poll_readable(client_.fd(), 0));
}

bool BackendControlServer::dispatch_one_message() {
    if (!client_)
        return false;

    std::array<std::byte, 2048> buffer {};
    int received_fd = -1;
    const auto rc = client_.receive_bytes(buffer.data(), buffer.size(), &received_fd);
    if (rc < 0) {
        if (received_fd >= 0)
            ::close(received_fd);
        std::fprintf(stderr, "sirenwm-wayland(display-server): control recv failed: %s\n",
            std::strerror(errno));
        disconnect_client();
        return false;
    }
    if (rc == 0) {
        if (received_fd >= 0)
            ::close(received_fd);
        std::fprintf(stderr, "sirenwm-wayland(display-server): control client closed socket\n");
        disconnect_client();
        return false;
    }
    if (static_cast<std::size_t>(rc) < sizeof(swm::ipc::MessageHeader)) {
        if (received_fd >= 0)
            ::close(received_fd);
        std::fprintf(stderr, "sirenwm-wayland(display-server): control packet too short (%zd)\n", rc);
        disconnect_client();
        return false;
    }

    swm::ipc::MessageHeader header {};
    std::memcpy(&header, buffer.data(), sizeof(header));

    const auto expected_size = sizeof(header) + header.size;
    if (header.magic != swm::ipc::kBackendProtocolMagic ||
        header.version != swm::ipc::kBackendProtocolVersion ||
        static_cast<std::size_t>(rc) != expected_size) {
        if (received_fd >= 0)
            ::close(received_fd);
        std::fprintf(stderr,
            "sirenwm-wayland(display-server): invalid control packet (magic=0x%x version=%u type=%u size=%u rc=%zd)\n",
            header.magic, header.version, static_cast<unsigned>(header.type), header.size, rc);
        disconnect_client();
        return false;
    }

    if (!dispatch_message(header, buffer.data() + sizeof(header), received_fd)) {
        if (received_fd >= 0)
            ::close(received_fd);
        std::fprintf(stderr,
            "sirenwm-wayland(display-server): rejected control message type=%u size=%u fd=%d\n",
            static_cast<unsigned>(header.type), header.size, received_fd);
        disconnect_client();
        return false;
    }

    return true;
}

bool BackendControlServer::send_snapshot() {
    const auto generation = ++snapshot_generation_;
    if (!send_event(swm::ipc::SnapshotBegin { .generation = generation }))
        return false;

    auto outputs = state_.snapshot_outputs();
    std::sort(outputs.begin(), outputs.end(),
        [](const DisplayState::OutputInfo& lhs, const DisplayState::OutputInfo& rhs) {
            return lhs.id < rhs.id;
        });
    for (const auto& output : outputs) {
        swm::ipc::OutputAdded msg {};
        msg.id = output.id;
        msg.name.assign(output.name.c_str());
        msg.x = output.x;
        msg.y = output.y;
        msg.width = output.w;
        msg.height = output.h;
        msg.refresh = output.refresh;
        if (!send_event(msg))
            return false;
    }

    auto surfaces = state_.snapshot_surfaces();
    std::sort(surfaces.begin(), surfaces.end(),
        [](const DisplaySurface& lhs, const DisplaySurface& rhs) {
            return lhs.id < rhs.id;
        });
    for (const auto& surface : surfaces) {
        swm::ipc::SurfaceCreated msg {};
        msg.id = surface.id;
        msg.app_id.assign(surface.app_id.c_str());
        msg.title.assign(surface.title.c_str());
        msg.pid = surface.pid;
        (void)send_event(msg);
        if (!client_)
            return false;
        if (surface.mapped)
            (void)send_event(swm::ipc::SurfaceMapped { .id = surface.id });
        if (!client_)
            return false;
        if (surface.width > 0 || surface.height > 0)
            (void)send_event(swm::ipc::SurfaceCommitted {
                .id = surface.id,
                .width = surface.width,
                .height = surface.height,
            });
        if (!client_)
            return false;
    }

    return send_event(swm::ipc::SnapshotEnd { .generation = generation });
}

bool BackendControlServer::dispatch_message(const swm::ipc::MessageHeader& header,
    const void* payload, int received_fd) {
    if (received_fd >= 0 && header.type != swm::ipc::BackendMessageType::UpdateOverlay)
        return false;

    return swm::ipc::dispatch_backend_message(header, payload, received_fd, *this);
}

bool BackendControlServer::on(const swm::ipc::Hello& msg) {
    if (msg.peer_role != swm::ipc::BackendPeerRole::WmController)
        return false;
    return send_event(swm::ipc::Hello {
        .peer_role = swm::ipc::BackendPeerRole::DisplayServerHost,
        .flags = 0,
    });
}

bool BackendControlServer::on(const swm::ipc::SnapshotRequest&) {
    return send_snapshot();
}

void BackendControlServer::on(const swm::ipc::ConfigureSurface& msg) {
    state_.configure_surface(msg.surface_id, msg.x, msg.y, msg.width, msg.height);
}

void BackendControlServer::on(const swm::ipc::SetSurfaceVisible& msg) {
    state_.set_surface_visible(msg.surface_id, msg.visible != 0);
}

void BackendControlServer::on(const swm::ipc::SetSurfaceActivated& msg) {
    state_.set_surface_activated(msg.surface_id, msg.activated != 0);
}

void BackendControlServer::on(const swm::ipc::SetSurfaceStacking& msg) {
    state_.set_surface_stacking(msg.surface_id, msg.raised);
}

void BackendControlServer::on(const swm::ipc::CloseSurface& msg) {
    state_.close_surface(msg.surface_id);
}

void BackendControlServer::on(const swm::ipc::WarpPointer& msg) {
    state_.warp_pointer(msg.x, msg.y);
}

void BackendControlServer::on(const swm::ipc::SetKeyboardIntercepts& msg) {
    const auto count = std::min<std::size_t>(msg.count, msg.intercepts.size());
    state_.set_keyboard_intercepts(std::span<const swm::ipc::KeyIntercept>(msg.intercepts.data(), count));
}

bool BackendControlServer::on(const swm::ipc::GrabPointer&, int received_fd) {
    if (received_fd >= 0)
        return false;
    state_.grab_pointer();
    return true;
}

bool BackendControlServer::on(const swm::ipc::UngrabPointer&, int received_fd) {
    if (received_fd >= 0)
        return false;
    state_.ungrab_pointer();
    return true;
}

void BackendControlServer::on(const swm::ipc::SetSurfaceBorder& msg) {
    state_.set_surface_border(msg.surface_id, msg.width, msg.color);
}

void BackendControlServer::on(const swm::ipc::CreateOverlay& msg) {
    state_.create_overlay(msg.overlay_id, msg.x, msg.y, msg.width, msg.height);
}

bool BackendControlServer::on(const swm::ipc::UpdateOverlay& msg, int received_fd) {
    if (received_fd < 0)
        return false;
    state_.update_overlay(msg.overlay_id, received_fd, msg.bytes);
    ::close(received_fd);
    return true;
}

void BackendControlServer::on(const swm::ipc::DestroyOverlay& msg) {
    state_.destroy_overlay(msg.overlay_id);
}

} // namespace wl::server
