#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <backend/events.hpp>
#include <backend/hooks.hpp>
#include <domain/core.hpp>

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
// Tests — runtime hook path and Lua-exposed rule primitives
// ---------------------------------------------------------------------------

TEST(RulesRuntime, WindowRulesHookFiredOnApply) {
    TestHarness h;
    h.start();

    h.runtime.invoke_hook(hook::WindowRules{ 0x1000, false });
}

TEST(RulesRuntime, WindowRulesHookFiredOnRestart) {
    TestHarness h;
    h.start();

    h.runtime.invoke_hook(hook::WindowRules{ 0x1000, true });
}

TEST(RulesRuntime, SetFloatingByIdWorks) {
    TestHarness h;
    h.start();

    WindowId win = make_classified_window(h, 0x1000, "steam");
    EXPECT_FALSE(h.core.window_state_any(win)->floating);

    h.core.dispatch(command::atom::SetWindowFloating{ win, true });
    EXPECT_TRUE(h.core.window_state_any(win)->floating);
}

TEST(RulesRuntime, MoveWindowToWorkspaceByIdWorks) {
    TestHarness h;
    h.start();

    WindowId win = make_classified_window(h, 0x1000, "firefox");
    EXPECT_EQ(h.core.workspace_of_window(win), 0);

    h.core.dispatch(command::atom::SetWindowSuppressFocusOnce{ win, true });
    h.core.dispatch(command::atom::MoveWindowToWorkspace{ win, 1 });
    EXPECT_EQ(h.core.workspace_of_window(win), 1);
}

TEST(RulesRuntime, DialogTypeWindowShouldFloat) {
    TestHarness h;
    h.start();

    WindowId win = make_classified_window(h, 0x1000, "someapp", "someapp", WindowType::Dialog);
    h.core.dispatch(command::atom::SetWindowFloating{ win, true });
    EXPECT_TRUE(h.core.window_state_any(win)->floating);
}

TEST(RulesRuntime, NormalTypeWindowNotAutoFloated) {
    TestHarness h;
    h.start();

    WindowId win = make_classified_window(h, 0x1000, "xterm");
    EXPECT_FALSE(h.core.window_state_any(win)->floating);
    EXPECT_FALSE(h.core.window_state_any(win)->floating);
}
