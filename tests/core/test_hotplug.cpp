#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <core.hpp>

#include "test_harness.hpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void apply_topology(Core& core, std::vector<Monitor> mons) {
    core.dispatch(command::atom::ApplyMonitorTopology{ std::move(mons) });
}

// ---------------------------------------------------------------------------
// Basic: single monitor topology update
// ---------------------------------------------------------------------------

TEST(Hotplug, ApplyTopologyWithSameMonitor) {
    TestHarness h;
    h.start();

    // Apply an identical topology — should not crash or lose windows
    WindowId win = h.map_window(0x1000, 0);

    apply_topology(h.core, {
        make_monitor(0, 0, 0, 1920, 1080, "primary"),
    });

    EXPECT_NE(h.core.window_state_any(win), nullptr);
}

TEST(Hotplug, WindowsPreservedAfterTopologyChange) {
    TestHarness h;
    h.start();

    WindowId a = h.map_window(0x2001, 0);
    WindowId b = h.map_window(0x2002, 0);

    apply_topology(h.core, {
        make_monitor(0, 0, 0, 2560, 1440, "primary"),
    });

    EXPECT_NE(h.core.window_state_any(a), nullptr);
    EXPECT_NE(h.core.window_state_any(b), nullptr);
}

// ---------------------------------------------------------------------------
// Add second monitor
// ---------------------------------------------------------------------------

TEST(Hotplug, AddSecondMonitorIncreasesCount) {
    TestHarness h;
    h.start();

    ASSERT_EQ((int)h.core.monitor_states().size(), 1);

    apply_topology(h.core, {
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });

    EXPECT_EQ((int)h.core.monitor_states().size(), 2);
}

TEST(Hotplug, EachNewMonitorGetsActiveWorkspaceWhenStartedWithTwo) {
    // Start with two monitors — workspaces distributed at init time.
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    // Both monitors must have a valid active workspace.
    EXPECT_GE(h.core.active_workspace_on_monitor(0), 0);
    EXPECT_GE(h.core.active_workspace_on_monitor(1), 0);
}

TEST(Hotplug, WindowsOnRemovedMonitorMigrated) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    // Map a window on monitor 1's workspace
    int      ws_on_mon1 = h.core.active_workspace_on_monitor(1);
    WindowId win        = h.map_window(0x3000, ws_on_mon1);

    // Remove monitor 1
    apply_topology(h.core, {
        make_monitor(0, 0, 0, 1920, 1080, "primary"),
    });

    // Window must still exist (moved to surviving monitor's pool)
    EXPECT_NE(h.core.window_state_any(win), nullptr);
}

// ---------------------------------------------------------------------------
// Remove monitor: workspaces re-parked, then restored on reconnect
// ---------------------------------------------------------------------------

TEST(Hotplug, WorkspacesParkedOnDisconnect) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    int monitors_before = (int)h.core.monitor_states().size();
    ASSERT_EQ(monitors_before, 2);

    // Disconnect monitor 1
    apply_topology(h.core, {
        make_monitor(0, 0, 0, 1920, 1080, "primary"),
    });

    EXPECT_EQ((int)h.core.monitor_states().size(), 1);
}

TEST(Hotplug, WorkspacesRestoredOnReconnect) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    int total_ws = h.core.workspace_count();

    // Disconnect monitor 1
    apply_topology(h.core, {
        make_monitor(0, 0, 0, 1920, 1080, "primary"),
    });

    // Reconnect monitor 1
    apply_topology(h.core, {
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });

    EXPECT_EQ((int)h.core.monitor_states().size(), 2);
    // Workspace count must not have grown beyond original
    EXPECT_EQ(h.core.workspace_count(), total_ws);
}

// ---------------------------------------------------------------------------
// Monitor inset commands
// ---------------------------------------------------------------------------

TEST(Hotplug, TopInsetAdjustsMonitorGeometry) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::atom::ApplyMonitorTopInset{ 20 });
    EXPECT_EQ(h.core.monitor_top_inset(), 20);
}

TEST(Hotplug, BottomInsetAdjustsMonitorGeometry) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::atom::ApplyMonitorBottomInset{ 24 });
    EXPECT_EQ(h.core.monitor_bottom_inset(), 24);
}

TEST(Hotplug, InsetIsIdempotentWhenSameValue) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::atom::ApplyMonitorTopInset{ 20 });
    h.core.dispatch(command::atom::ApplyMonitorTopInset{ 20 }); // same value twice — no-op
    EXPECT_EQ(h.core.monitor_top_inset(), 20);
}

// ---------------------------------------------------------------------------
// MoveWindowToMonitor
// ---------------------------------------------------------------------------

TEST(Hotplug, MoveWindowToMonitorChangesWorkspace) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    int ws_mon0 = h.core.active_workspace_on_monitor(0);
    int ws_mon1 = h.core.active_workspace_on_monitor(1);
    ASSERT_NE(ws_mon0, ws_mon1);

    WindowId win = h.map_window(0x4000, ws_mon0);
    EXPECT_EQ(h.core.workspace_of_window(win), ws_mon0);

    h.core.dispatch(command::atom::MoveWindowToMonitor{ win, 1 });

    // Window should now be on monitor 1's active workspace
    EXPECT_EQ(h.core.workspace_of_window(win), ws_mon1);
}

TEST(Hotplug, MoveWindowToSameMonitorIsNoop) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    int      ws0 = h.core.active_workspace_on_monitor(0);
    WindowId win = h.map_window(0x5000, ws0);

    // Move to same monitor (0) — should stay on same workspace
    h.core.dispatch(command::atom::MoveWindowToMonitor{ win, 0 });
    EXPECT_EQ(h.core.workspace_of_window(win), ws0);
}

// ---------------------------------------------------------------------------
// DisplayTopologyChanged domain event emitted
// ---------------------------------------------------------------------------

TEST(Hotplug, TopologyChangeEmitsDomainEvent) {
    TestHarness h;
    h.start();
    h.core.take_core_events(); // drain any startup events

    apply_topology(h.core, {
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });

    auto evts  = h.core.take_core_events();
    bool found = false;
    for (const auto& ev : evts) {
        if (std::holds_alternative<event::DisplayTopologyChanged>(ev)) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}
