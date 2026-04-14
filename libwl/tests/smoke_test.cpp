#include <wl/wl.hpp>
#include <cstdio>

int main() {
    // Server-side: create display, get event loop, destroy.
    {
        wl::Display display;
        auto        loop = display.event_loop();
        printf("display created, event loop fd=%d\n", loop.fd());
    }

    printf("libwl smoke test passed\n");
    return 0;
}
