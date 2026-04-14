#pragma once

#include <string>

namespace swm::ipc {

std::string runtime_dir();
std::string backend_socket_path(const std::string& wayland_display);
std::string backend_socket_path_from_env();

} // namespace swm::ipc
