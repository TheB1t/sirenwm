#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <domain/core.hpp>

#include "core_harness.hpp"

static std::optional<WindowId> first_focus_effect(const std::vector<BackendEffect>& effects) {
    for (const auto& effect : effects)
        if (effect.kind == BackendEffectKind::FocusWindow)
            return effect.window;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Basic focus
// ---------------------------------------------------------------------------

TEST(Focus, FocusWindowSetsState) {
    CoreHarness h;
    h.start();

    WindowId a = h.map_window(0x1001, 0);
    WindowId b = h.map_window(0x1002, 0);

    h.core.dispatch(command::atom::FocusWindow{ a });
    EXPECT_EQ(h.core.focus_state().window, a);

    h.core.dispatch(command::atom::FocusWindow{ b });
    EXPECT_EQ(h.core.focus_state().window, b);
}

TEST(Focus, FocusWindowOnNonExistentIsNoop) {
    CoreHarness h;
    h.start();

    WindowId a = h.map_window(0x1001, 0);
    h.core.dispatch(command::atom::FocusWindow{ a });
    h.core.take_backend_effects();

    EXPECT_FALSE(h.core.dispatch(command::atom::FocusWindow{ 0xDEAD }));
    EXPECT_EQ(h.core.focus_state().window, a);
    EXPECT_FALSE(first_focus_effect(h.core.take_backend_effects()).has_value());
}

// ---------------------------------------------------------------------------
// FocusNextWindow / FocusPrevWindow cycling
// ---------------------------------------------------------------------------

TEST(Focus, FocusNextCyclesThroughWindows) {
    CoreHarness h;
    h.start();

    WindowId a = h.map_window(0x2001, 0);
    WindowId b = h.map_window(0x2002, 0);
    WindowId c = h.map_window(0x2003, 0);

    h.core.dispatch(command::atom::FocusWindow{ a });
    h.core.take_backend_effects(); // drain
    h.core.dispatch(command::composite::FocusNextWindow{});

    auto focused_win = first_focus_effect(h.core.take_backend_effects());
    ASSERT_TRUE(focused_win.has_value());
    EXPECT_EQ(*focused_win, b);
    EXPECT_EQ(h.core.focus_state().window, b);
    (void)c;
}

TEST(Focus, FocusPrevCyclesThroughWindows) {
    CoreHarness h;
    h.start();

    WindowId a = h.map_window(0x3001, 0);
    WindowId b = h.map_window(0x3002, 0);
    WindowId c = h.map_window(0x3003, 0);

    h.core.dispatch(command::atom::FocusWindow{ a });
    h.core.take_backend_effects();
    h.core.dispatch(command::composite::FocusPrevWindow{});

    auto focused_win = first_focus_effect(h.core.take_backend_effects());
    ASSERT_TRUE(focused_win.has_value());
    EXPECT_EQ(*focused_win, c);
    EXPECT_EQ(h.core.focus_state().window, c);
    (void)b;
}

TEST(Focus, FocusNextWrapsAround) {
    CoreHarness h;
    h.start();

    WindowId a = h.map_window(0x4001, 0);
    WindowId b = h.map_window(0x4002, 0);

    h.core.dispatch(command::atom::FocusWindow{ a });
    h.core.take_backend_effects();

    h.core.dispatch(command::composite::FocusNextWindow{});
    auto first = first_focus_effect(h.core.take_backend_effects());
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, b);
    EXPECT_EQ(h.core.focus_state().window, b);

    h.core.dispatch(command::composite::FocusNextWindow{});
    auto second = first_focus_effect(h.core.take_backend_effects());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, a);
    EXPECT_EQ(h.core.focus_state().window, a);
}

TEST(Focus, FocusNextOnSingleWindowStaysFocused) {
    CoreHarness h;
    h.start();

    WindowId a = h.map_window(0x5001, 0);
    h.core.dispatch(command::atom::FocusWindow{ a });
    h.core.take_backend_effects();

    EXPECT_TRUE(h.core.dispatch(command::composite::FocusNextWindow{}));

    auto focused = first_focus_effect(h.core.take_backend_effects());
    ASSERT_TRUE(focused.has_value());
    EXPECT_EQ(*focused, a);
    EXPECT_EQ(h.core.focus_state().window, a);
}

// ---------------------------------------------------------------------------
// Focus after workspace switch
// ---------------------------------------------------------------------------

TEST(Focus, FocusRestoredAfterWorkspaceSwitch) {
    CoreHarness h;
    h.start();

    WindowId a = h.map_window(0x6001, 0);
    WindowId b = h.map_window(0x6002, 0);
    h.core.dispatch(command::atom::FocusWindow{ b });
    h.core.take_backend_effects();

    h.core.dispatch(command::atom::SwitchWorkspace{ 1, std::nullopt });
    h.core.take_backend_effects();
    h.core.dispatch(command::atom::SwitchWorkspace{ 0, std::nullopt });

    auto focused = first_focus_effect(h.core.take_backend_effects());
    ASSERT_TRUE(focused.has_value());
    EXPECT_EQ(*focused, b);
    EXPECT_EQ(h.core.focus_state().window, b);
    (void)a;
}

TEST(Focus, FocusFollowsActiveMonitorAfterSwitch) {
    CoreHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    h.core.dispatch(command::atom::FocusMonitor{ 0 });
    EXPECT_EQ(h.core.focused_monitor_index(), 0);

    h.core.dispatch(command::atom::FocusMonitor{ 1 });
    EXPECT_EQ(h.core.focused_monitor_index(), 1);
}

// ---------------------------------------------------------------------------
// SuppressFocusOnce
// ---------------------------------------------------------------------------

TEST(Focus, SuppressFocusOncePreventsFocusOnFirstCall) {
    CoreHarness h;
    h.start();

    WindowId win = h.map_window(0x7001, 0);
    h.core.dispatch(command::atom::SetWindowSuppressFocusOnce{ win, true });

    // consume_window_suppress_focus_once returns true → caller should skip focus
    EXPECT_TRUE(h.core.consume_window_suppress_focus_once(win));
}

TEST(Focus, SuppressFocusOnceConsumedOnce) {
    CoreHarness h;
    h.start();

    WindowId win = h.map_window(0x7002, 0);
    h.core.dispatch(command::atom::SetWindowSuppressFocusOnce{ win, true });

    h.core.consume_window_suppress_focus_once(win); // consume
    // second call: flag is gone
    EXPECT_FALSE(h.core.consume_window_suppress_focus_once(win));
}

TEST(Focus, SuppressFocusOnceDefaultFalse) {
    CoreHarness h;
    h.start();

    WindowId win = h.map_window(0x7003, 0);
    // Never set the flag — should return false immediately
    EXPECT_FALSE(h.core.consume_window_suppress_focus_once(win));
}

// ---------------------------------------------------------------------------
// Remove window: focus falls back to another window on same ws
// ---------------------------------------------------------------------------

TEST(Focus, FocusFallbackAfterWindowRemoval) {
    CoreHarness h;
    h.start();

    WindowId a = h.map_window(0x8001, 0);
    WindowId b = h.map_window(0x8002, 0);
    h.core.dispatch(command::atom::FocusWindow{ b });

    h.core.dispatch(command::atom::RemoveWindowFromAllWorkspaces{ b });
    EXPECT_EQ(h.core.window_state_any(b), nullptr);
    EXPECT_NE(h.core.window_state_any(a), nullptr);
    EXPECT_EQ(h.core.focus_state().window, a);
}

// ---------------------------------------------------------------------------
// ToggleFocusedWindowFloating
// ---------------------------------------------------------------------------

TEST(Focus, ToggleFocusedWindowFloating) {
    CoreHarness h;
    h.start();

    WindowId win = h.map_window(0x9001, 0);
    h.core.dispatch(command::atom::FocusWindow{ win });

    bool was = h.core.window_state_any(win)->floating;
    h.core.dispatch(command::composite::ToggleFocusedWindowFloating{});
    EXPECT_NE(h.core.window_state_any(win)->floating, was);
}
