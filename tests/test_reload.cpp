#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <core.hpp>

#include "test_harness.hpp"

// ---------------------------------------------------------------------------
// Reload: workspace state preserved
// ---------------------------------------------------------------------------

TEST(Reload, WindowsNotLostAfterReload) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    ASSERT_NE(h.core.window_state_any(win), nullptr);

    // Simulate a config reload: snapshot → restore reload state
    auto snap = h.core.snapshot_reload_state();
    h.core.clear_reloadable_runtime_state();
    h.core.restore_reload_state(snap);

    // Window must still be tracked after reload
    EXPECT_NE(h.core.window_state_any(win), nullptr);
}

TEST(Reload, WorkspaceCountCanGrow) {
    TestHarness h;
    h.start();

    int before = h.core.workspace_count();

    CoreSettings s;
    s.workspace_defs.resize((size_t)before + 2);
    for (int i = 0; i < (int)s.workspace_defs.size(); ++i)
        s.workspace_defs[(size_t)i] = { "[" + std::to_string(i + 1) + "]", "" };
    h.core.apply_settings(s);

    EXPECT_EQ(h.core.workspace_count(), before + 2);
}

TEST(Reload, WorkspaceCountCanShrink) {
    TestHarness h;
    h.start();

    int before = h.core.workspace_count();
    ASSERT_GE(before, 2);

    CoreSettings s;
    s.workspace_defs = { { "[1]", "" } };
    h.core.apply_settings(s);

    EXPECT_EQ(h.core.workspace_count(), 1);
}

TEST(Reload, WindowsMigratedFromRemovedWorkspace) {
    TestHarness h;
    h.start();

    // Map window on last workspace
    int last_ws = h.core.workspace_count() - 1;
    WindowId win = h.map_window(0x1000, last_ws);
    EXPECT_EQ(h.core.workspace_of_window(win), last_ws);

    // Shrink to remove that workspace
    CoreSettings s;
    s.workspace_defs = { { "[1]", "" } };
    h.core.apply_settings(s);

    // Window must still exist, migrated to a valid workspace
    EXPECT_NE(h.core.window_state_any(win), nullptr);
    EXPECT_LT(h.core.workspace_of_window(win), h.core.workspace_count());
}

TEST(Reload, ActiveWorkspaceClampedAfterShrink) {
    TestHarness h;
    h.start();

    // Switch to last workspace
    int last_ws = h.core.workspace_count() - 1;
    h.core.dispatch(command::SwitchWorkspace{ last_ws, std::nullopt });
    EXPECT_EQ(h.core.active_workspace_on_monitor(0), last_ws);

    // Shrink to 1 workspace
    CoreSettings s;
    s.workspace_defs = { { "[1]", "" } };
    h.core.apply_settings(s);

    // Active workspace must be within bounds
    EXPECT_LT(h.core.active_workspace_on_monitor(0), h.core.workspace_count());
}
