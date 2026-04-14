#include <swm/ipc/endpoint.hpp>

#include <cstdlib>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

namespace swm::ipc {

std::string runtime_dir() {
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg)
        return xdg;

    std::string dir = "/tmp/sirenwm-runtime-" + std::to_string(getuid());
    mkdir(dir.c_str(), 0700);
    return dir;
}

std::string backend_socket_path(const std::string& wayland_display) {
    if (wayland_display.empty())
        return {};
    return runtime_dir() + "/" + wayland_display + ".sirenwm-ipc";
}

std::string backend_socket_path_from_env() {
    if (const char* socket = std::getenv("SIRENWM_IPC_SOCKET"); socket && *socket)
        return socket;
    if (const char* wayland_display = std::getenv("WAYLAND_DISPLAY"); wayland_display && *wayland_display)
        return backend_socket_path(wayland_display);
    return {};
}

} // namespace swm::ipc
