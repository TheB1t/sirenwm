#include <wl/server/x11/xwayland.hpp>

#include <wl/server/x11/xwayland_shell.hpp>
#include <wl/server/x11/xwm.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
#include <wayland-server-core.h>
}

static int set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int create_lockfile(int display) {
    char path[64];
    snprintf(path, sizeof(path), "/tmp/.X%d-lock", display);

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0444);
    if (fd < 0) return -1;

    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%10d\n", getpid());
    if (write(fd, buf, len) != len) { close(fd); unlink(path); return -1; }
    close(fd);
    return 0;
}

static int open_socket(const char* path) {
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    const size_t path_len = std::strlen(path);
    if (path_len >= sizeof(addr.sun_path))
        return -1;
    std::memcpy(addr.sun_path, path, path_len + 1);

    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    set_cloexec(fd);

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 1) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool XWayland::open_display_sockets() {
    for (int i = 0; i < 32; i++) {
        char lock_path[64];
        snprintf(lock_path, sizeof(lock_path), "/tmp/.X%d-lock", i);

        if (access(lock_path, F_OK) == 0)
            continue;

        if (create_lockfile(i) < 0)
            continue;

        char path0[108];
        snprintf(path0, sizeof(path0), "/tmp/.X11-unix/X%d", i);

        x_fd_[0] = open_socket(path0);
        if (x_fd_[0] < 0) {
            unlink(lock_path);
            continue;
        }

        display_num_ = i;
        display_name_ = ":" + std::to_string(i);
        return true;
    }
    return false;
}

void XWayland::unlink_display_sockets() {
    if (display_num_ < 0) return;

    char path[64];
    snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", display_num_);
    unlink(path);
    snprintf(path, sizeof(path), "/tmp/.X%d-lock", display_num_);
    unlink(path);
}

static bool xwayland_available() {
    return access("/usr/bin/Xwayland", X_OK) == 0 ||
           access("/usr/local/bin/Xwayland", X_OK) == 0;
}

XWayland::XWayland(wl::Display& display, wl::server::Compositor& compositor,
                   XwmSurfaceSink& sink, int output_width, int output_height)
    : display_(display), compositor_(compositor), sink_(sink)
    , output_width_(output_width), output_height_(output_height) {
    if (!xwayland_available()) {
        fprintf(stderr, "xwayland: Xwayland binary not found, X11 app support disabled\n");
        return;
    }
    if (!open_display_sockets()) {
        fprintf(stderr, "xwayland: failed to find free display\n");
        return;
    }
    if (!launch()) {
        fprintf(stderr, "xwayland: failed to launch Xwayland\n");
        unlink_display_sockets();
        return;
    }
    fprintf(stderr, "xwayland: launching on DISPLAY=%s\n", display_name_.c_str());
}

XWayland::~XWayland() {
    xwm_.reset();
    shell_.reset();

    if (ready_source_)
        wl_event_source_remove(ready_source_);

    if (xwl_client_)
        wl_client_destroy(xwl_client_);

    if (xwl_pid_ > 0) {
        kill(xwl_pid_, SIGTERM);
        xwl_pid_ = -1;
    }

    for (int i = 0; i < 2; i++) {
        if (wl_fd_[i] >= 0) close(wl_fd_[i]);
        if (wm_fd_[i] >= 0) close(wm_fd_[i]);
        if (x_fd_[i] >= 0) close(x_fd_[i]);
    }

    unlink_display_sockets();
}

bool XWayland::launch() {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, wl_fd_) < 0) return false;
    set_cloexec(wl_fd_[0]);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, wm_fd_) < 0) return false;
    set_cloexec(wm_fd_[0]);

    xwl_client_ = wl_client_create(display_.raw(), wl_fd_[0]);
    if (!xwl_client_) return false;
    wl_fd_[0] = -1;

    int notify_fd[2];
    if (pipe(notify_fd) < 0) return false;
    set_cloexec(notify_fd[0]);

    auto* wl_loop = wl_display_get_event_loop(display_.raw());
    ready_source_ = wl_event_loop_add_fd(wl_loop, notify_fd[0],
                                          WL_EVENT_READABLE,
                                          &XWayland::ready_callback, this);

    pid_t pid = fork();
    if (pid < 0) {
        close(notify_fd[0]);
        close(notify_fd[1]);
        return false;
    }

    if (pid == 0) {
        char wl_fd_str[16];
        char wm_fd_str[16];
        char x_fd_str[16];
        char notify_str[16];
        snprintf(wl_fd_str, sizeof(wl_fd_str), "%d", wl_fd_[1]);
        snprintf(wm_fd_str, sizeof(wm_fd_str), "%d", wm_fd_[1]);
        snprintf(x_fd_str, sizeof(x_fd_str), "%d", x_fd_[0]);
        snprintf(notify_str, sizeof(notify_str), "%d", notify_fd[1]);

        fcntl(wl_fd_[1], F_SETFD, 0);
        fcntl(wm_fd_[1], F_SETFD, 0);
        fcntl(x_fd_[0], F_SETFD, 0);
        fcntl(notify_fd[1], F_SETFD, 0);

        setenv("WAYLAND_SOCKET", wl_fd_str, 1);

        signal(SIGUSR1, SIG_IGN);

        execlp("Xwayland", "Xwayland",
               display_name_.c_str(),
               "-rootless",
               "-terminate",
               "-listenfd", x_fd_str,
               "-displayfd", notify_str,
               "-wm", wm_fd_str,
               nullptr);

        fprintf(stderr, "xwayland: exec failed: %s\n", strerror(errno));
        _exit(1);
    }

    xwl_pid_ = pid;
    close(wl_fd_[1]); wl_fd_[1] = -1;
    close(wm_fd_[1]); wm_fd_[1] = -1;
    close(notify_fd[1]);

    return true;
}

int XWayland::ready_callback(int fd, uint32_t, void* data) {
    auto* self = static_cast<XWayland*>(data);

    char buf[64] = {};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);

    close(fd);
    wl_event_source_remove(self->ready_source_);
    self->ready_source_ = nullptr;

    if (n <= 0) {
        fprintf(stderr, "xwayland: Xwayland died before becoming ready\n");
        self->cleanup_dead_child();
        return 0;
    }

    self->on_ready();
    return 0;
}

void XWayland::cleanup_dead_child() {
    if (xwl_client_) {
        wl_client_destroy(xwl_client_);
        xwl_client_ = nullptr;
    }
    if (xwl_pid_ > 0) {
        waitpid(xwl_pid_, nullptr, WNOHANG);
        xwl_pid_ = -1;
    }
    for (int i = 0; i < 2; i++) {
        if (wm_fd_[i] >= 0) { close(wm_fd_[i]); wm_fd_[i] = -1; }
    }
    unlink_display_sockets();
}

void XWayland::on_ready() {
    fprintf(stderr, "xwayland: Xwayland ready on %s\n", display_name_.c_str());
    ready_ = true;

    shell_ = std::make_unique<XWaylandShell>(
        display_.raw(), xwl_client_, compositor_);

    xwm_ = std::make_unique<XWindowManager>(
        wm_fd_[0], xwl_client_, *shell_, compositor_, sink_,
        output_width_, output_height_);
    wm_fd_[0] = -1;
}

int XWayland::xcb_fd() const {
    return xwm_ ? xwm_->fd() : -1;
}

void XWayland::dispatch() {
    if (xwm_) xwm_->dispatch();
}

void XWayland::configure_window(uint32_t surface_id, int32_t x, int32_t y,
                                 int32_t w, int32_t h) {
    if (xwm_) xwm_->configure(surface_id, x, y, w, h);
}

void XWayland::close_window(uint32_t surface_id) {
    if (xwm_) xwm_->close(surface_id);
}

void XWayland::activate_window(uint32_t surface_id, bool activated) {
    if (xwm_) xwm_->activate(surface_id, activated);
}

bool XWayland::is_xwayland_surface(uint32_t surface_id) const {
    return xwm_ && xwm_->owns(surface_id);
}
