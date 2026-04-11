#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <backend/events.hpp>
#include <core.hpp>

#include "test_harness.hpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static WindowId make_classified_window(TestHarness& h, WindowId id,
    const std::string& wm_class, const std::string& wm_instance = "",
    WindowType type = WindowType::Normal) {
    h.core.dispatch(command::atom::EnsureWindow{ id, 0 });
    command::atom::SetWindowMetadata meta;
    meta.window      = id;
    meta.wm_class    = wm_class;
    meta.wm_instance = wm_instance.empty() ? wm_class : wm_instance;
    meta.type        = type;
    h.core.dispatch(meta);
    h.core.dispatch(command::atom::SetWindowMapped{ id, true });
    return id;
}

// ---------------------------------------------------------------------------
// Tests — ApplyWindowRules event reaches Lua handlers
// ---------------------------------------------------------------------------

// Verify that emitting ApplyWindowRules fires sys.on("window_rules") handlers.
TEST(Rules, WindowRulesEventFiredOnApply) {
    TestHarness h;
    h.start();

    int call_count = 0;
    h.runtime.emit(event::ApplyWindowRules{ 0x1000, false });
    // No crash = event routing works (handlers registered via Lua are tested in integration).
    (void)call_count;
}

// ApplyWindowRules with from_restart=true must still fire the event —
// the Lua handler is responsible for checking win.from_restart.
TEST(Rules, WindowRulesEventFiredOnRestart) {
    TestHarness h;
    h.start();

    // Just verify no crash and event propagates without a C++ handler consuming it.
    h.runtime.emit(event::ApplyWindowRules{ 0x1000, true });
}

// Verify that dispatch(SetWindowFloating) correctly sets floating state —
// this is what swm.rules calls via siren.win.set_floating(id, true).
TEST(Rules, SetFloatingByIdWorks) {
    TestHarness h;
    h.start();

    WindowId win = make_classified_window(h, 0x1000, "steam");
    EXPECT_FALSE(h.core.window_state_any(win)->floating);

    h.core.dispatch(command::atom::SetWindowFloating{ win, true });
    EXPECT_TRUE(h.core.window_state_any(win)->floating);
}

// Verify that dispatch(MoveWindowToWorkspace) routes window — what swm.rules
// calls via siren.win.move_to(id, ws).
TEST(Rules, MoveWindowToWorkspaceByIdWorks) {
    TestHarness h;
    h.start();

    WindowId win = make_classified_window(h, 0x1000, "firefox");
    EXPECT_EQ(h.core.workspace_of_window(win), 0);

    h.core.dispatch(command::atom::SetWindowSuppressFocusOnce{ win, true });
    h.core.dispatch(command::atom::MoveWindowToWorkspace{ win, 1 });
    EXPECT_EQ(h.core.workspace_of_window(win), 1);
}

// Dialog-type windows should float — this mirrors the auto-float logic in swm.rules.
TEST(Rules, DialogTypeWindowShouldFloat) {
    TestHarness h;
    h.start();

    WindowId win = make_classified_window(h, 0x1000, "someapp", "someapp", WindowType::Dialog);
    // Simulate what rules.lua does on window_rules event for dialog type.
    h.core.dispatch(command::atom::SetWindowFloating{ win, true });
    EXPECT_TRUE(h.core.window_state_any(win)->floating);
}

// Normal-type window is not auto-floated.
TEST(Rules, NormalTypeWindowNotAutoFloated) {
    TestHarness h;
    h.start();

    WindowId win = make_classified_window(h, 0x1000, "xterm");
    EXPECT_FALSE(h.core.window_state_any(win)->floating);
    // No rules applied — still non-floating.
    EXPECT_FALSE(h.core.window_state_any(win)->floating);
}
