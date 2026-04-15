#include <display_server_backend.hpp>
#include <display_server_ports.hpp>

#include <backend/commands.hpp>
#include <backend/events.hpp>
#include <backend/hooks.hpp>
#include <config/config_types.hpp>
#include <domain/core.hpp>
#include <support/log.hpp>
#include <runtime/runtime.hpp>
#include <swm/ipc/endpoint.hpp>
#include <swm/ipc/message_dispatch.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>

namespace {

bool poll_readable(int fd, int timeout_ms) {
    if (fd < 0)
        return false;

    pollfd pfd {
        .fd      = fd,
        .events  = POLLIN,
        .revents = 0,
    };
    const int rc = ::poll(&pfd, 1, timeout_ms);
    return rc > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

uint32_t parse_hex_color(const std::string& s) {
    if (s.size() == 7 && s[0] == '#') {
        char*          end = nullptr;
        const uint32_t rgb = static_cast<uint32_t>(std::strtoul(s.c_str() + 1, &end, 16));
        if (end == s.c_str() + 7)
            return 0xFF000000 | rgb;
    }
    return 0xFF888888;
}

} // namespace

DisplayServerBackend::DisplayServerBackend(Core&, Runtime& runtime)
    : input_(*this)
      , monitor_(*this)
      , render_(*this)
      , keyboard_()
      , runtime_(runtime) {
    const auto control_socket = swm::ipc::backend_socket_path_from_env();
    if (control_socket.empty())
        throw std::runtime_error("display_server_backend: SIRENWM_IPC_SOCKET/WAYLAND_DISPLAY is not set");

    control_ = swm::ipc::Channel::connect_seqpacket(control_socket);
    if (!control_)
        throw std::runtime_error("display_server_backend: failed to connect control IPC");

    send_control(swm::ipc::Hello {
        .peer_role = swm::ipc::BackendPeerRole::WmController,
        .flags     = 0,
    });
    send_control(swm::ipc::SnapshotRequest {});
    bootstrap_snapshot();
}

DisplayServerBackend::~DisplayServerBackend() {
    shutdown();
}

void DisplayServerBackend::shutdown() {
    started_ = false;
    control_.close();
    surfaces_.clear();
    outputs_.clear();
    prev_focused_ = NO_WINDOW;
}

int DisplayServerBackend::event_fd() const {
    return control_ ? control_.fd() : -1;
}

void DisplayServerBackend::bootstrap_snapshot() {
    snapshot_complete_ = false;
    while (!snapshot_complete_) {
        if (!poll_readable(control_.fd(), 3000))
            throw std::runtime_error("display_server_backend: timed out waiting for control snapshot");

        std::array<std::byte, 2048> buffer {};
        int                         received_fd = -1;
        const auto                  rc          = control_.receive_bytes(buffer.data(), buffer.size(), &received_fd);
        if (rc < 0) {
            if (received_fd >= 0)
                ::close(received_fd);
            throw std::runtime_error(std::string("display_server_backend: control IPC recv failed: ") + std::strerror(errno));
        }
        if (rc == 0) {
            if (received_fd >= 0)
                ::close(received_fd);
            throw std::runtime_error("display_server_backend: control IPC closed during snapshot");
        }
        if (static_cast<std::size_t>(rc) < sizeof(swm::ipc::MessageHeader)) {
            if (received_fd >= 0)
                ::close(received_fd);
            throw std::runtime_error("display_server_backend: control snapshot packet too short");
        }

        swm::ipc::MessageHeader header {};
        std::memcpy(&header, buffer.data(), sizeof(header));
        const auto              expected_size = sizeof(header) + header.size;
        if (header.magic != swm::ipc::kBackendProtocolMagic ||
            header.version != swm::ipc::kBackendProtocolVersion ||
            static_cast<std::size_t>(rc) != expected_size) {
            if (received_fd >= 0)
                ::close(received_fd);
            throw std::runtime_error("display_server_backend: invalid control snapshot packet");
        }
        if (!dispatch_control_message(header, buffer.data() + sizeof(header), received_fd)) {
            if (received_fd >= 0)
                ::close(received_fd);
            throw std::runtime_error("display_server_backend: control snapshot dispatch failed");
        }
        if (received_fd >= 0)
            ::close(received_fd);
    }
}

void DisplayServerBackend::pump_events(std::size_t max_events_per_tick) {
    if (!control_)
        return;
    if (!poll_readable(control_.fd(), 0))
        return;

    std::size_t processed = 0;
    do {
        std::array<std::byte, 2048> buffer {};
        int                         received_fd = -1;
        const auto                  rc          = control_.receive_bytes(buffer.data(), buffer.size(), &received_fd);
        if (rc < 0) {
            if (received_fd >= 0)
                ::close(received_fd);
            throw std::runtime_error(std::string("display_server_backend: control IPC recv failed: ") + std::strerror(errno));
        }
        if (rc == 0) {
            if (received_fd >= 0)
                ::close(received_fd);
            throw std::runtime_error("display_server_backend: control IPC closed");
        }
        if (static_cast<std::size_t>(rc) < sizeof(swm::ipc::MessageHeader)) {
            if (received_fd >= 0)
                ::close(received_fd);
            throw std::runtime_error("display_server_backend: control event packet too short");
        }

        swm::ipc::MessageHeader header {};
        std::memcpy(&header, buffer.data(), sizeof(header));
        const auto              expected_size = sizeof(header) + header.size;
        if (header.magic != swm::ipc::kBackendProtocolMagic ||
            header.version != swm::ipc::kBackendProtocolVersion ||
            static_cast<std::size_t>(rc) != expected_size) {
            if (received_fd >= 0)
                ::close(received_fd);
            throw std::runtime_error("display_server_backend: invalid control event packet");
        }
        if (!dispatch_control_message(header, buffer.data() + sizeof(header), received_fd)) {
            if (received_fd >= 0)
                ::close(received_fd);
            throw std::runtime_error(
                "display_server_backend: unsupported control event (msg=" +
                std::to_string(static_cast<uint16_t>(header.type)) + ")");
        }
        if (received_fd >= 0)
            ::close(received_fd);

        ++processed;
    } while (control_ &&
        (max_events_per_tick == 0 || processed < max_events_per_tick) &&
        poll_readable(control_.fd(), 0));
}

bool DisplayServerBackend::dispatch_control_message(const swm::ipc::MessageHeader& header,
    const void* payload, int received_fd) {
    if (received_fd >= 0)
        return false;
    return swm::ipc::dispatch_backend_message(header, payload, received_fd, *this);
}

bool DisplayServerBackend::on(const swm::ipc::Hello& msg) {
    return msg.peer_role == swm::ipc::BackendPeerRole::DisplayServerHost;
}

void DisplayServerBackend::on(const swm::ipc::SnapshotBegin&) {
    snapshot_complete_ = false;
    surfaces_.clear();
    outputs_.clear();
    prev_focused_ = NO_WINDOW;
}

void DisplayServerBackend::on(const swm::ipc::SnapshotEnd&) {
    snapshot_complete_ = true;
}

void DisplayServerBackend::on(const swm::ipc::OutputAdded& msg) {
    outputs_[msg.id] = { msg.id, msg.name.bytes, msg.x, msg.y, msg.width, msg.height, msg.refresh };
    if (started_)
        runtime_.post_event(event::DisplayTopologyChanged {});
}

void DisplayServerBackend::on(const swm::ipc::OutputRemoved& msg) {
    outputs_.erase(msg.id);
    if (started_)
        runtime_.post_event(event::DisplayTopologyChanged {});
}

void DisplayServerBackend::on(const swm::ipc::SurfaceCreated& msg) {
    surfaces_[msg.id] = { msg.id, msg.app_id.bytes, msg.title.bytes, msg.pid };
}

void DisplayServerBackend::on(const swm::ipc::SurfaceMapped& msg) {
    auto it = surfaces_.find(msg.id);
    if (it == surfaces_.end())
        return;
    it->second.mapped = true;
    if (started_ && !it->second.managed && it->second.width > 0 && it->second.height > 0)
        manage_surface(it->second);
}

void DisplayServerBackend::on(const swm::ipc::SurfaceUnmapped& msg) {
    auto it = surfaces_.find(msg.id);
    if (it != surfaces_.end())
        it->second.mapped = false;
}

void DisplayServerBackend::on(const swm::ipc::SurfaceDestroyed& msg) {
    const auto wid = static_cast<WindowId>(msg.id);
    auto       it  = surfaces_.find(msg.id);
    if (it == surfaces_.end())
        return;

    if (!started_ || !it->second.managed) {
        if (prev_focused_ == wid)
            prev_focused_ = NO_WINDOW;
        surfaces_.erase(it);
        return;
    }

    LOG_INFO("DisplayServerBackend: surface %u destroyed", msg.id);
    auto& core        = runtime_.core;
    bool  was_focused = false;
    if (auto focused = core.focused_window_state())
        was_focused = (focused->id == wid);
    bool was_visible = false;
    {
        const WorkspaceId ws_id = core.workspace_of_window(wid);
        was_visible = (ws_id >= 0) && core.is_workspace_visible(ws_id);
    }

    runtime_.post_event(event::DestroyNotify { wid });
    (void)core.dispatch(command::atom::RemoveWindowFromAllWorkspaces { wid });
    runtime_.post_event(event::WindowUnmapped { wid, true });

    if (was_visible) {
        (void)core.dispatch(command::atom::ReconcileNow {});
        if (was_focused) {
            if (auto next = core.focused_window_state(); next && next->is_visible())
                (void)core.dispatch(command::atom::FocusWindow { next->id });
            else
                (void)core.dispatch(command::composite::FocusNextWindow {});
        }
    }

    if (prev_focused_ == wid)
        prev_focused_ = NO_WINDOW;

    surfaces_.erase(it);
}

void DisplayServerBackend::on(const swm::ipc::SurfaceCommitted& msg) {
    auto it = surfaces_.find(msg.id);
    if (it == surfaces_.end())
        return;
    it->second.width  = msg.width;
    it->second.height = msg.height;

    if (started_ && it->second.mapped && !it->second.managed && msg.width > 0 && msg.height > 0)
        manage_surface(it->second);
}

void DisplayServerBackend::on(const swm::ipc::SurfaceTitleChanged& msg) {
    auto it = surfaces_.find(msg.id);
    if (it == surfaces_.end())
        return;
    it->second.title = msg.title.bytes;
    if (started_ && it->second.managed)
        runtime_.post_event(event::PropertyNotify { static_cast<WindowId>(msg.id), 0 });
}

void DisplayServerBackend::on(const swm::ipc::SurfaceAppIdChanged& msg) {
    auto it = surfaces_.find(msg.id);
    if (it != surfaces_.end())
        it->second.app_id = msg.app_id.bytes;
}

void DisplayServerBackend::on(const swm::ipc::Key& msg) {
    if (!started_ || msg.pressed == 0)
        return;

    runtime_.post_event(event::KeyPressEv {
        static_cast<uint16_t>(msg.mods),
        static_cast<uint8_t>(msg.keycode),
        msg.keysym,
    });
}

void DisplayServerBackend::on(const swm::ipc::Button& msg) {
    if (!started_)
        return;

    if (!msg.released && msg.surface_id != 0) {
        auto&      core = runtime_.core;
        const auto wid  = static_cast<WindowId>(msg.surface_id);
        if (auto window = core.window_state_any(wid); window && window->is_visible())
            (void)core.dispatch(command::atom::FocusWindow { wid });
    }

    runtime_.post_event(event::ButtonEv {
        .window    = static_cast<WindowId>(msg.surface_id),
        .root      = static_cast<WindowId>(msg.surface_id),
        .root_pos  = { static_cast<int16_t>(msg.x), static_cast<int16_t>(msg.y) },
        .event_pos = { static_cast<int16_t>(msg.x), static_cast<int16_t>(msg.y) },
        .time      = 0,
        .button    = static_cast<uint8_t>(msg.button - 0x110 + 1),
        .state     = static_cast<uint16_t>(msg.mods),
        .release   = msg.released != 0,
    });
}

void DisplayServerBackend::on(const swm::ipc::PointerMotion& msg) {
    if (!started_)
        return;

    runtime_.post_event(event::MotionEv {
        static_cast<WindowId>(msg.surface_id),
        { static_cast<int16_t>(msg.x), static_cast<int16_t>(msg.y) },
        static_cast<uint16_t>(msg.mods),
    });
}

void DisplayServerBackend::on(const swm::ipc::PointerEnter& msg) {
    if (!started_)
        return;

    auto&      core   = runtime_.core;
    const auto wid    = static_cast<WindowId>(msg.surface_id);
    auto       window = core.window_state_any(wid);
    if (!window || !window->is_visible())
        return;

    core.dispatch(command::atom::FocusWindow { wid });
}

void DisplayServerBackend::on(const swm::ipc::OverlayExpose& msg) {
    if (!started_)
        return;
    runtime_.post_event(event::ExposeWindow { static_cast<WindowId>(msg.overlay_id) });
}

void DisplayServerBackend::on(const swm::ipc::OverlayButton& msg) {
    if (!started_)
        return;

    runtime_.post_event(event::ButtonEv {
        .window    = static_cast<WindowId>(msg.overlay_id),
        .root      = static_cast<WindowId>(msg.overlay_id),
        .root_pos  = { static_cast<int16_t>(msg.x), static_cast<int16_t>(msg.y) },
        .event_pos = { static_cast<int16_t>(msg.x), static_cast<int16_t>(msg.y) },
        .time      = 0,
        .button    = static_cast<uint8_t>(msg.button - 0x110 + 1),
        .state     = 0,
        .release   = msg.released != 0,
    });
}

void DisplayServerBackend::render_frame() {
    auto& core    = runtime_.core;
    auto  effects = core.take_backend_effects();

    for (const auto& effect : effects) {
        switch (effect.kind) {
            case BackendEffectKind::MapWindow: {
                auto ws = core.window_state(effect.window);
                if (!ws)
                    break;
                configure_surface(effect.window,
                    ws->pos().x(), ws->pos().y(),
                    ws->size().x(), ws->size().y());
                set_surface_visible(effect.window, 1);
                if (ws->border_width > 0)
                    set_surface_border(effect.window, ws->border_width, unfocused_border_);
                if (auto flush = core.take_window_flush(effect.window))
                    (void)flush;
                break;
            }
            case BackendEffectKind::UnmapWindow:
                set_surface_visible(effect.window, 0);
                break;
            case BackendEffectKind::FocusWindow:
                if (prev_focused_ != NO_WINDOW && prev_focused_ != effect.window)
                    set_surface_activated(prev_focused_, 0);
                if (effect.window != NO_WINDOW) {
                    set_surface_activated(effect.window, 1);
                    set_surface_border(effect.window, 0, 0);
                    auto ws = core.window_state(effect.window);
                    if (ws && ws->border_width > 0)
                        set_surface_border(effect.window, ws->border_width, focused_border_);
                }
                if (prev_focused_ != NO_WINDOW && prev_focused_ != effect.window) {
                    auto prev = core.window_state(prev_focused_);
                    if (prev && prev->border_width > 0)
                        set_surface_border(prev_focused_, prev->border_width, unfocused_border_);
                }
                prev_focused_ = effect.window;
                break;
            case BackendEffectKind::FocusRoot:
                if (prev_focused_ != NO_WINDOW) {
                    set_surface_activated(prev_focused_, 0);
                    auto prev = core.window_state(prev_focused_);
                    if (prev && prev->border_width > 0)
                        set_surface_border(prev_focused_, prev->border_width, unfocused_border_);
                    prev_focused_ = NO_WINDOW;
                }
                break;
            case BackendEffectKind::UpdateWindow: {
                if (effect.window == NO_WINDOW)
                    break;
                if (auto flush = core.take_window_flush(effect.window)) {
                    auto ws = core.window_state(effect.window);
                    if (ws) {
                        configure_surface(effect.window,
                            ws->pos().x(), ws->pos().y(),
                            ws->size().x(), ws->size().y());
                        const uint32_t color = (effect.window == prev_focused_)
                        ? focused_border_ : unfocused_border_;
                        set_surface_border(effect.window, ws->border_width, color);
                    }
                }
                break;
            }
            case BackendEffectKind::RaiseWindow:
                set_surface_stacking(effect.window, 1);
                break;
            case BackendEffectKind::LowerWindow:
                set_surface_stacking(effect.window, 0);
                break;
            case BackendEffectKind::CloseWindow:
                close_surface(effect.window);
                break;
            case BackendEffectKind::WarpPointer:
                warp_pointer(effect.pos.x(), effect.pos.y());
                break;
        }
    }

    for (auto win : core.visible_window_ids()) {
        if (auto flush = core.take_window_flush(win)) {
            auto ws = core.window_state(win);
            if (ws) {
                configure_surface(win,
                    ws->pos().x(), ws->pos().y(),
                    ws->size().x(), ws->size().y());
            }
        }
    }
}

void DisplayServerBackend::on_start(Core&) {
    reload_border_colors();
    started_ = true;

    for (auto& [_, surface] : surfaces_) {
        if (surface.mapped && !surface.managed && surface.width > 0 && surface.height > 0)
            manage_surface(surface);
    }
}

void DisplayServerBackend::reload_border_colors() {
    auto&       theme     = runtime_.core.current_settings().theme;
    const auto& focused   = theme.border_focused.empty() ? theme.accent : theme.border_focused;
    const auto& unfocused = theme.border_unfocused.empty()
        ? (theme.alt_bg.empty() ? theme.bg : theme.alt_bg)
        : theme.border_unfocused;
    focused_border_   = parse_hex_color(focused);
    unfocused_border_ = parse_hex_color(unfocused);
}

backend::BackendPorts DisplayServerBackend::ports() {
    return { input_, monitor_, render_, keyboard_ };
}

void DisplayServerBackend::configure_surface(WindowId id, int32_t x, int32_t y, int32_t w, int32_t h) {
    send_control(swm::ipc::ConfigureSurface {
        .surface_id = static_cast<uint32_t>(id),
        .x          = x,
        .y          = y,
        .width      = w,
        .height     = h,
    });
}

void DisplayServerBackend::set_surface_activated(WindowId id, uint32_t activated) {
    send_control(swm::ipc::SetSurfaceActivated {
        .surface_id = static_cast<uint32_t>(id),
        .activated  = activated,
    });
}

void DisplayServerBackend::set_surface_visible(WindowId id, uint32_t visible) {
    send_control(swm::ipc::SetSurfaceVisible {
        .surface_id = static_cast<uint32_t>(id),
        .visible    = visible,
    });
}

void DisplayServerBackend::set_surface_stacking(WindowId id, uint32_t stacking) {
    send_control(swm::ipc::SetSurfaceStacking {
        .surface_id = static_cast<uint32_t>(id),
        .raised     = stacking,
    });
}

void DisplayServerBackend::close_surface(WindowId id) {
    send_control(swm::ipc::CloseSurface {
        .surface_id = static_cast<uint32_t>(id),
    });
}

void DisplayServerBackend::set_keyboard_intercepts(const void* data, std::size_t size_bytes) {
    swm::ipc::SetKeyboardIntercepts msg {};
    const auto                      count = std::min<std::size_t>(
        size_bytes / sizeof(backend::KeyIntercept),
        msg.intercepts.size());
    msg.count = static_cast<uint32_t>(count);
    if (data && count > 0)
        std::memcpy(msg.intercepts.data(), data, count * sizeof(backend::KeyIntercept));
    send_control(msg);
}

void DisplayServerBackend::warp_pointer(int32_t x, int32_t y) {
    send_control(swm::ipc::WarpPointer { .x = x, .y = y });
}

void DisplayServerBackend::grab_pointer() {
    send_control(swm::ipc::GrabPointer {});
}

void DisplayServerBackend::ungrab_pointer() {
    send_control(swm::ipc::UngrabPointer {});
}

void DisplayServerBackend::set_surface_border(WindowId id, uint32_t width, uint32_t color) {
    send_control(swm::ipc::SetSurfaceBorder {
        .surface_id = static_cast<uint32_t>(id),
        .width      = width,
        .color      = color,
    });
}

void DisplayServerBackend::create_overlay(uint32_t overlay_id, int32_t x, int32_t y, int32_t w, int32_t h) {
    send_control(swm::ipc::CreateOverlay {
        .overlay_id = overlay_id,
        .x          = x,
        .y          = y,
        .width      = w,
        .height     = h,
    });
}

void DisplayServerBackend::update_overlay(uint32_t overlay_id, int fd, uint32_t bytes) {
    try {
        send_control(swm::ipc::UpdateOverlay {
            .overlay_id = overlay_id,
            .bytes      = bytes,
        }, fd);
    } catch (...) {
        if (fd >= 0)
            ::close(fd);
        throw;
    }
    if (fd >= 0)
        ::close(fd);
}

void DisplayServerBackend::destroy_overlay(uint32_t overlay_id) {
    send_control(swm::ipc::DestroyOverlay {
        .overlay_id = overlay_id,
    });
}

void DisplayServerBackend::manage_surface(DisplayServerSurfaceInfo& surface) {
    if (surface.managed)
        return;
    surface.managed = true;
    LOG_INFO("DisplayServerBackend: surface %u mapped", surface.id);

    auto&      core = runtime_.core;
    const auto wid  = static_cast<WindowId>(surface.id);

    core.dispatch(command::atom::EnsureWindow { .window = wid });

    core.dispatch(command::atom::SetWindowMetadata {
        .window      = wid,
        .wm_instance = surface.app_id,
        .wm_class    = surface.app_id,
        .title       = surface.title,
        .pid         = surface.pid,
        .type        = WindowType::Normal,
        .hints       = {},
    });

    core.dispatch(command::atom::SetWindowGeometry {
        .window = wid,
        .pos    = { surface.x, surface.y },
        .size   = { surface.width, surface.height },
    });

    runtime_.invoke_hook(hook::WindowRules { wid });

    core.dispatch(command::atom::MapWindow { wid });
    runtime_.post_event(event::WindowMapped { wid });
    core.dispatch(command::atom::ReconcileNow {});

    auto mapped = core.window_state(wid);
    if (mapped && mapped->is_visible())
        core.dispatch(command::atom::FocusWindow { wid });
}
