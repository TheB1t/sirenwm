#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <domain/core.hpp>
#include <domain/layout.hpp>

#include "core_harness.hpp"

// ---------------------------------------------------------------------------
// SetLayout
// ---------------------------------------------------------------------------

TEST(Layout, SetLayoutTileSucceeds) {
    CoreHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::SetLayout{ "tile" });
    EXPECT_TRUE(ok);
}

TEST(Layout, SetLayoutMonocleSucceeds) {
    CoreHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::SetLayout{ "monocle" });
    EXPECT_TRUE(ok);
}

TEST(Layout, SetLayoutUnknownReturnsFalse) {
    CoreHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::SetLayout{ "nonexistent" });
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// SetMasterFactor — clamped to [0.1, 0.9]
// ---------------------------------------------------------------------------

TEST(Layout, SetMasterFactorStored) {
    CoreHarness h;
    h.start();

    h.core.dispatch(command::atom::SetMasterFactor{ 0.65f });
    EXPECT_NEAR(h.core.cfg().master_factor, 0.65f, 0.001f);
}

TEST(Layout, SetMasterFactorClampedLow) {
    CoreHarness h;
    h.start();

    h.core.dispatch(command::atom::SetMasterFactor{ 0.0f });
    EXPECT_NEAR(h.core.cfg().master_factor, 0.1f, 0.001f);
}

TEST(Layout, SetMasterFactorClampedHigh) {
    CoreHarness h;
    h.start();

    h.core.dispatch(command::atom::SetMasterFactor{ 1.0f });
    EXPECT_NEAR(h.core.cfg().master_factor, 0.9f, 0.001f);
}

// ---------------------------------------------------------------------------
// AdjustMasterFactor — delta adjusts from current
// ---------------------------------------------------------------------------

TEST(Layout, AdjustMasterFactorDelta) {
    CoreHarness h;
    h.start();

    h.core.dispatch(command::atom::SetMasterFactor{ 0.5f });
    float before = h.core.cfg().master_factor;

    h.core.dispatch(command::composite::AdjustMasterFactor{ 0.1f });
    EXPECT_NEAR(h.core.cfg().master_factor, before + 0.1f, 0.001f);
}

TEST(Layout, AdjustMasterFactorClampedAtBoundary) {
    CoreHarness h;
    h.start();

    h.core.dispatch(command::atom::SetMasterFactor{ 0.85f });
    h.core.dispatch(command::composite::AdjustMasterFactor{ 0.1f }); // would exceed 0.9
    EXPECT_NEAR(h.core.cfg().master_factor, 0.9f, 0.001f);
}

// ---------------------------------------------------------------------------
// IncMaster — changes nmaster (never goes below 1)
// ---------------------------------------------------------------------------

TEST(Layout, IncMasterIncrements) {
    CoreHarness h;
    h.start();

    int before = h.core.cfg().nmaster;
    h.core.dispatch(command::composite::IncMaster{ 1 });
    EXPECT_EQ(h.core.cfg().nmaster, before + 1);
}

TEST(Layout, IncMasterDecrements) {
    CoreHarness h;
    h.start();

    h.core.dispatch(command::composite::IncMaster{ 2 }); // nmaster = 3
    int before = h.core.cfg().nmaster;
    h.core.dispatch(command::composite::IncMaster{ -1 });
    EXPECT_EQ(h.core.cfg().nmaster, before - 1);
}

TEST(Layout, IncMasterNeverBelowOne) {
    CoreHarness h;
    h.start();

    // Try to drive nmaster below 1
    h.core.dispatch(command::composite::IncMaster{ -100 });
    EXPECT_GE(h.core.cfg().nmaster, 1);
}

// ---------------------------------------------------------------------------
// Zoom — swaps focused window with master
// ---------------------------------------------------------------------------

TEST(Layout, ZoomWithNoWindowsIsFalse) {
    CoreHarness h;
    h.start();

    bool ok = h.core.dispatch(command::composite::Zoom{});
    // no windows — workspace empty — zoom returns false
    EXPECT_FALSE(ok);
}

TEST(Layout, ZoomWithSingleWindowReturnsFalse) {
    CoreHarness h;
    h.start();

    WindowId a = h.map_window(0x1001, 0);
    h.core.dispatch(command::atom::FocusWindow{ a });

    // Single tiled window: zoom_focused requires >= 2 tiled windows.
    bool ok = h.core.dispatch(command::composite::Zoom{});
    EXPECT_FALSE(ok);
}

TEST(Layout, ZoomMovesNonMasterToFront) {
    CoreHarness h;
    h.start();

    WindowId a = h.map_window(0x2001, 0);
    WindowId b = h.map_window(0x2002, 0);
    WindowId c = h.map_window(0x2003, 0);

    // Focus c (not master) and zoom — c should become master
    h.core.dispatch(command::atom::FocusWindow{ c });

    auto ws_before = h.core.workspace_state(0);
    ASSERT_NE(ws_before, nullptr);
    // a is the first window (master) before zoom
    EXPECT_EQ(ws_before->windows[0]->id, a);

    bool ok = h.core.dispatch(command::composite::Zoom{});
    EXPECT_TRUE(ok);

    auto ws_after = h.core.workspace_state(0);
    ASSERT_NE(ws_after, nullptr);
    // After zoom, c should be at position 0 (master)
    EXPECT_EQ(ws_after->windows[0]->id, c);
    (void)b; // referenced to silence warning
}

// ---------------------------------------------------------------------------
// SwitchWorkspaceLocalIndex
// ---------------------------------------------------------------------------

TEST(Layout, SwitchLocalIndexChangesWorkspace) {
    CoreHarness h({
        make_monitor(0, 0, 0, 1920, 1080, "primary"),
    });
    h.start();

    // Monitor 0 owns multiple workspaces ([1],[2],[3])
    // local index 0 is already active; switch to 1
    int ws_before = h.core.active_workspace_on_monitor(0);
    h.core.dispatch(command::composite::SwitchWorkspaceLocalIndex{ 1 });
    int ws_after = h.core.active_workspace_on_monitor(0);

    EXPECT_NE(ws_after, ws_before);
}

// ---------------------------------------------------------------------------
// ReconcileNow — a no-op arrange, should not crash
// ---------------------------------------------------------------------------

TEST(Layout, ReconcileNowIsIdempotent) {
    CoreHarness h;
    h.start();

    h.map_window(0x9001, 0);
    h.map_window(0x9002, 0);

    // Two consecutive reconciles should produce consistent state
    h.core.dispatch(command::atom::ReconcileNow{});
    h.core.dispatch(command::atom::ReconcileNow{});

    EXPECT_NE(h.core.window_state_any(0x9001), nullptr);
    EXPECT_NE(h.core.window_state_any(0x9002), nullptr);
}
