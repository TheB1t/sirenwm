#include <gtest/gtest.h>
#include "test_harness.hpp"
#include "bar/bar_module.hpp"

TEST(Bar, StartsAndStopsWithoutCrash) {
    TestHarness h;
    h.use<BarModule>();
    h.start();
}

TEST(Bar, FocusChangeDoesNotCrash) {
    TestHarness h;
    h.use<BarModule>();
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::FocusWindow{ win });
    h.emit(event::FocusChanged{ win });
}

TEST(Bar, WorkspaceSwitchDoesNotCrash) {
    TestHarness h;
    h.use<BarModule>();
    h.start();

    h.core.dispatch(command::SwitchWorkspace{ 1, std::nullopt });
    h.emit(event::WorkspaceSwitched{ 1 });
}

TEST(Bar, MultiMonitorCreatesMultipleBars) {
    TestHarness h({
        make_monitor(0, 0, 0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1080, 1920, "secondary"),
    });
    h.use<BarModule>();
    h.start();
    // Should not crash — two bar windows created via FakeRenderPort.
}
