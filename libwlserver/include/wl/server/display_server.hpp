#pragma once

namespace wl::server {

struct DisplayServerOptions {
    int width  = 1280;
    int height = 720;
};

// Run the embedded Wayland display server loop.
// Returns EXIT_SUCCESS / EXIT_FAILURE compatible codes.
int run_display_server(const DisplayServerOptions& options);

} // namespace wl::server
