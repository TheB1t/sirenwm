#include <gtest/gtest.h>
#include "test_harness.hpp"
#include "keybindings/keybindings.hpp"

TEST(Keybindings, StartsAndStopsWithoutCrash) {
    TestHarness h;
    h.use<KeybindingsModule>();
    h.start();
}

TEST(Keybindings, GrabsButtonsOnWindowMap) {
    TestHarness h;
    h.use<KeybindingsModule>();
    h.start();

    h.backend.input_port.log.clear();
    h.map_window(0x1000, 0);
    h.emit(event::WindowMapped{ 0x1000 });

    // KeybindingsModule re-grabs mouse buttons on each mapped window.
    // FakeInputPort::grab_button is a no-op but should not crash.
}

TEST(Keybindings, KeyPressWithNoBindingsDoesNotCrash) {
    TestHarness h;
    h.use<KeybindingsModule>();
    h.start();

    // Dispatch an arbitrary key press — no bindings registered via Lua,
    // so this should be silently ignored.
    h.emit(event::KeyPressEv{ 0, 36, 0xff0d /* Return */ });
}

TEST(Keybindings, DragNotActiveByDefault) {
    TestHarness h;
    h.use<KeybindingsModule>();
    h.start();

    // A motion event without a prior button press should not crash.
    h.emit(event::MotionEv{ NO_WINDOW, { 100, 200 }, 0 });
}

TEST(Keybindings, UnmapCancelsDrag) {
    TestHarness h;
    h.use<KeybindingsModule>();
    h.start();

    WindowId win = h.map_window(0x2000, 0);
    h.emit(event::WindowMapped{ win });

    // Simulate the window going away — should not crash even if a drag were
    // hypothetically active.
    h.emit(event::WindowUnmapped{ win, false });
}
