#include <wl_display.hpp>
#include <log.hpp>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <unistd.h>

WlDisplay::WlDisplay() {
    display_ = wl_display_create();
    if (!display_)
        LOG_ERR("WlDisplay: wl_display_create failed");

    ev_loop_ = wl_display_get_event_loop(display_);

    init_socket();
}

WlDisplay::~WlDisplay() {
    if (display_)
        wl_display_destroy(display_);
}

int WlDisplay::event_fd() const noexcept {
    return wl_event_loop_get_fd(ev_loop_);
}

void WlDisplay::flush_clients() noexcept {
    wl_display_flush_clients(display_);
}

void WlDisplay::dispatch_events() noexcept {
    wl_event_loop_dispatch(ev_loop_, 0);
}

void WlDisplay::prepare_exec_restart() noexcept {
    if (socket_fd_ < 0 || socket_name_.empty()) {
        LOG_WARN("WlDisplay: prepare_exec_restart: no socket fd tracked, clients will die");
        return;
    }
    // Drop O_CLOEXEC so the fd survives execv().
    int flags = fcntl(socket_fd_, F_GETFD);
    if (flags < 0 || fcntl(socket_fd_, F_SETFD, flags & ~FD_CLOEXEC) < 0) {
        LOG_ERR("WlDisplay: prepare_exec_restart: fcntl failed: %s", std::strerror(errno));
        return;
    }
    // Pass fd number and socket name to the new process via environment.
    setenv("SIRENWM_WL_SOCKET_FD",   std::to_string(socket_fd_).c_str(), 1);
    setenv("SIRENWM_WL_SOCKET_NAME", socket_name_.c_str(), 1);
    LOG_INFO("WlDisplay: socket fd=%d name=%s will survive exec",
        socket_fd_, socket_name_.c_str());
}

void WlDisplay::init_socket() {
    const char* inherited_fd_str   = std::getenv("SIRENWM_WL_SOCKET_FD");
    const char* inherited_name_str = std::getenv("SIRENWM_WL_SOCKET_NAME");

    if (inherited_fd_str && inherited_name_str) {
        int inherited_fd = std::atoi(inherited_fd_str);
        // Restore O_CLOEXEC now that we've consumed the fd.
        fcntl(inherited_fd, F_SETFD, FD_CLOEXEC);
        if (wl_display_add_socket_fd(display_, inherited_fd) == 0) {
            socket_fd_   = inherited_fd;
            socket_name_ = inherited_name_str;
            setenv("WAYLAND_DISPLAY", socket_name_.c_str(), 1);
            LOG_INFO("WlDisplay: inherited socket fd=%d name=%s", inherited_fd, socket_name_.c_str());
        } else {
            LOG_ERR("WlDisplay: wl_display_add_socket_fd(%d) failed", inherited_fd);
        }
        return;
    }

    const char* socket_name = wl_display_add_socket_auto(display_);
    if (!socket_name) {
        LOG_ERR("WlDisplay: wl_display_add_socket_auto failed");
        return;
    }

    socket_name_ = socket_name;

    // libwayland does not expose the socket fd directly; locate it via
    // /proc/self/fd by matching the socket path in XDG_RUNTIME_DIR.
    const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime) {
        std::string sock_path = std::string(xdg_runtime) + "/" + socket_name_;
        char        link_buf[256];
        for (int fd = 3; fd < 1024; ++fd) {
            std::string proc_fd = "/proc/self/fd/" + std::to_string(fd);
            ssize_t     n       = readlink(proc_fd.c_str(), link_buf, sizeof(link_buf) - 1);
            if (n > 0) {
                link_buf[n] = '\0';
                if (sock_path == link_buf) {
                    socket_fd_ = fd;
                    break;
                }
            }
        }
    }

    setenv("WAYLAND_DISPLAY", socket_name_.c_str(), 1);
    LOG_INFO("WlDisplay: listening on %s (fd=%d)", socket_name_.c_str(), socket_fd_);
}
