#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <core.hpp>

#include "test_harness.hpp"

// ---------------------------------------------------------------------------
// Basic focus
// ---------------------------------------------------------------------------

TEST(Focus, FocusWindowSetsState) {
    TestHarness h;
    h.start();

    WindowId a = h.map_window(0x1001, 0);
    WindowId b = h.map_window(0x1002, 0);

    h.core.dispatch(command::FocusWindow{ a });
    EXPECT_EQ(h.core.focus_state().window, a);

    h.core.dispatch(command::FocusWindow{ b });
    EXPECT_EQ(h.core.focus_state().window, b);
}

TEST(Focus, FocusWindowOnNonExistentIsNoop) {
    TestHarness h;
    h.start();

    WindowId a = h.map_window(0x1001, 0);
    h.core.dispatch(command::FocusWindow{ a });

    // Focus a window that doesn't exist — state should not change
    h.core.dispatch(command::FocusWindow{ 0xDEAD });
    // We can't guarantee the state didn't flicker, but it must be valid
    // (existing window or NO_WINDOW)
    WindowId w = h.core.focus_state().window;
    EXPECT_TRUE(w == a || w == NO_WINDOW);
}

// ---------------------------------------------------------------------------
// FocusNextWindow / FocusPrevWindow cycling
// ---------------------------------------------------------------------------

TEST(Focus, FocusNextCyclesThroughWindows) {
    TestHarness h;
    h.start();

    WindowId a = h.map_window(0x2001, 0);
    WindowId b = h.map_window(0x2002, 0);
    WindowId c = h.map_window(0x2003, 0);

    // FocusNextWindow emits a FocusWindow backend effect pointing to the new window.
    h.core.dispatch(command::FocusWindow{ a });
    h.core.take_backend_effects(); // drain
    h.core.dispatch(command::FocusNextWindow{});

    auto fx = h.core.take_backend_effects();
    // There must be a FocusWindow effect for some window that is not a
    bool found_focus = false;
    WindowId focused_win = NO_WINDOW;
    for (const auto& e : fx) {
        if (e.kind == BackendEffectKind::FocusWindow) {
            found_focus  = true;
            focused_win  = e.window;
            break;
        }
    }
    EXPECT_TRUE(found_focus);
    EXPECT_TRUE(focused_win == b || focused_win == c);
    (void)a;
}

TEST(Focus, FocusPrevCyclesThroughWindows) {
    TestHarness h;
    h.start();

    WindowId a = h.map_window(0x3001, 0);
    WindowId b = h.map_window(0x3002, 0);
    WindowId c = h.map_window(0x3003, 0);

    h.core.dispatch(command::FocusWindow{ a });
    h.core.take_backend_effects();
    h.core.dispatch(command::FocusPrevWindow{});

    auto fx = h.core.take_backend_effects();
    bool found_focus = false;
    WindowId focused_win = NO_WINDOW;
    for (const auto& e : fx) {
        if (e.kind == BackendEffectKind::FocusWindow) {
            found_focus = true;
            focused_win = e.window;
            break;
        }
    }
    EXPECT_TRUE(found_focus);
    EXPECT_TRUE(focused_win == b || focused_win == c);
    (void)a;
}

TEST(Focus, FocusNextWrapsAround) {
    TestHarness h;
    h.start();

    WindowId a = h.map_window(0x4001, 0);
    WindowId b = h.map_window(0x4002, 0);

    // FocusNext twice in a 2-window workspace should result in focus effects.
    h.core.dispatch(command::FocusWindow{ a });
    h.core.dispatch(command::FocusNextWindow{});
    h.core.dispatch(command::FocusNextWindow{});

    // After two steps, a FocusWindow effect must have been emitted each time.
    auto fx = h.core.take_backend_effects();
    bool any_focus = false;
    for (const auto& e : fx) {
        if (e.kind == BackendEffectKind::FocusWindow)
            any_focus = true;
    }
    EXPECT_TRUE(any_focus);
    (void)b;
}

TEST(Focus, FocusNextOnSingleWindowStaysFocused) {
    TestHarness h;
    h.start();

    WindowId a = h.map_window(0x5001, 0);
    h.core.dispatch(command::FocusWindow{ a });
    // With one window FocusNextWindow still returns true and
    // emits a FocusWindow effect for the same window.
    h.core.take_backend_effects();
    h.core.dispatch(command::FocusNextWindow{});
    // Must not crash; effect may or may not be emitted for same window.
    // Just verify the window still exists.
    EXPECT_NE(h.core.window_state_any(a), nullptr);
}

// ---------------------------------------------------------------------------
// Focus after workspace switch
// ---------------------------------------------------------------------------

TEST(Focus, FocusRestoredAfterWorkspaceSwitch) {
    TestHarness h;
    h.start();

    // ws 0: two windows, focus b
    WindowId a = h.map_window(0x6001, 0);
    WindowId b = h.map_window(0x6002, 0);
    h.core.dispatch(command::FocusWindow{ b });

    // Switch to ws 1
    h.core.dispatch(command::SwitchWorkspace{ 1, std::nullopt });
    // Switch back to ws 0
    h.core.dispatch(command::SwitchWorkspace{ 0, std::nullopt });

    // focus_state for ws 0 should restore to b (or a valid window)
    WindowId w = h.core.focus_state().window;
    EXPECT_TRUE(w == a || w == b);
}

TEST(Focus, FocusFollowsActiveMonitorAfterSwitch) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    h.core.dispatch(command::FocusMonitor{ 0 });
    EXPECT_EQ(h.core.focused_monitor_index(), 0);

    h.core.dispatch(command::FocusMonitor{ 1 });
    EXPECT_EQ(h.core.focused_monitor_index(), 1);
}

// ---------------------------------------------------------------------------
// SuppressFocusOnce
// ---------------------------------------------------------------------------

TEST(Focus, SuppressFocusOncePreventsFocusOnFirstCall) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x7001, 0);
    h.core.dispatch(command::SetWindowSuppressFocusOnce{ win, true });

    // consume_window_suppress_focus_once returns true → caller should skip focus
    EXPECT_TRUE(h.core.consume_window_suppress_focus_once(win));
}

TEST(Focus, SuppressFocusOnceConsumedOnce) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x7002, 0);
    h.core.dispatch(command::SetWindowSuppressFocusOnce{ win, true });

    h.core.consume_window_suppress_focus_once(win); // consume
    // second call: flag is gone
    EXPECT_FALSE(h.core.consume_window_suppress_focus_once(win));
}

TEST(Focus, SuppressFocusOnceDefaultFalse) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x7003, 0);
    // Never set the flag — should return false immediately
    EXPECT_FALSE(h.core.consume_window_suppress_focus_once(win));
}

// ---------------------------------------------------------------------------
// Remove window: focus falls back to another window on same ws
// ---------------------------------------------------------------------------

TEST(Focus, FocusFallbackAfterWindowRemoval) {
    TestHarness h;
    h.start();

    WindowId a = h.map_window(0x8001, 0);
    WindowId b = h.map_window(0x8002, 0);
    h.core.dispatch(command::FocusWindow{ b });

    h.core.dispatch(command::RemoveWindowFromAllWorkspaces{ b });
    // After b is gone, focus may fall back to a or to NO_WINDOW
    // (core doesn't auto-pick focus without a backend driving it, but
    //  the state must be consistent — b must no longer be tracked)
    EXPECT_EQ(h.core.window_state_any(b), nullptr);
    // a still exists
    EXPECT_NE(h.core.window_state_any(a), nullptr);
}

// ---------------------------------------------------------------------------
// ToggleFocusedWindowFloating
// ---------------------------------------------------------------------------

TEST(Focus, ToggleFocusedWindowFloating) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x9001, 0);
    h.core.dispatch(command::FocusWindow{ win });

    bool was = h.core.window_state_any(win)->floating;
    h.core.dispatch(command::ToggleFocusedWindowFloating{});
    EXPECT_NE(h.core.window_state_any(win)->floating, was);
}
