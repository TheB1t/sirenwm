#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <core.hpp>

#include "test_harness.hpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool has_effect(const std::vector<BackendEffect>& effects,
    BackendEffectKind kind, WindowId win = NO_WINDOW) {
    for (const auto& e : effects)
        if (e.kind == kind && (win == NO_WINDOW || e.window == win))
            return true;
    return false;
}

static std::vector<BackendEffect> drain(Core& core) {
    return core.take_backend_effects();
}

static std::unique_ptr<TestHarness> make_dual() {
    auto h = std::make_unique<TestHarness>(std::vector<Monitor>{
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 2560, 1440, "secondary"),
    });
    CoreSettings s;
    s.workspace_defs = { { "[1]", "primary" }, { "[2]", "secondary" }, { "[3]", "" } };
    h->core.apply_settings(s);
    h->start();
    return h;
}

// ---------------------------------------------------------------------------
// FocusMonitor switches focus across monitors
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, FocusMonitorSwitchesCorrectly) {
    auto h = make_dual();

    h->core.dispatch(command::atom::FocusMonitor{ 0 });
    EXPECT_EQ(h->core.focused_monitor_index(), 0);

    h->core.dispatch(command::atom::FocusMonitor{ 1 });
    EXPECT_EQ(h->core.focused_monitor_index(), 1);
}

TEST(MultiMonitorFocus, FocusMonitorEmitsFocusRootOnOldMonitor) {
    auto h = make_dual();

    h->core.dispatch(command::atom::FocusMonitor{ 0 });
    drain(h->core);

    h->core.dispatch(command::atom::FocusMonitor{ 1 });
    auto effects = drain(h->core);

    // When switching monitors, X focus is cleared first (FocusRoot)
    EXPECT_TRUE(has_effect(effects, BackendEffectKind::FocusRoot));
}

// ---------------------------------------------------------------------------
// Windows on each monitor independently managed
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, WindowsOnDifferentMonitorsIndependent) {
    auto     h = make_dual();

    WindowId w1 = h->map_window(0x1000, 0);
    WindowId w2 = h->map_window(0x2000, 1);

    // Both windows exist and are on their respective workspaces
    EXPECT_EQ(h->core.workspace_of_window(w1), 0);
    EXPECT_EQ(h->core.workspace_of_window(w2), 1);

    // Focusing w1 doesn't affect w2's existence
    h->core.dispatch(command::atom::FocusWindow{ w1 });
    EXPECT_NE(h->core.window_state_any(w2), nullptr);
}

// ---------------------------------------------------------------------------
// Workspace switch on one monitor doesn't affect other
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, WorkspaceSwitchIsolatedPerMonitor) {
    auto     h = make_dual();

    WindowId w1 = h->map_window(0x1000, 0); // ws 0 on monitor 0
    WindowId w2 = h->map_window(0x2000, 1); // ws 1 on monitor 1

    // Switch workspace on monitor 0 to ws 2
    h->core.dispatch(command::atom::FocusMonitor{ 0 });
    h->core.dispatch(command::atom::SwitchWorkspace{ 2, 0 });

    // w1 should now be hidden (ws 0 is no longer active on monitor 0)
    auto s1 = h->core.window_state_any(w1);
    EXPECT_FALSE(s1->is_visible());

    // w2 on monitor 1 should still be visible
    auto s2 = h->core.window_state_any(w2);
    EXPECT_TRUE(s2->is_visible());
}

// ---------------------------------------------------------------------------
// Focus follows window moved to visible workspace on other monitor
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, FocusFollowsWindowToOtherMonitor) {
    auto     h = make_dual();

    WindowId win = h->map_window(0x1000, 0);
    h->core.dispatch(command::atom::FocusWindow{ win });
    drain(h->core);

    h->core.dispatch(command::atom::MoveWindowToWorkspace{ win, 1 });
    auto effects = drain(h->core);

    // Window moved to visible ws on monitor 1 — should get focus
    EXPECT_TRUE(has_effect(effects, BackendEffectKind::FocusWindow, win));
}

// ---------------------------------------------------------------------------
// MoveWindowToMonitor resolves monitor's active workspace
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, MoveToMonitorUsesActiveWorkspace) {
    auto     h = make_dual();

    WindowId win = h->map_window(0x1000, 0);
    h->core.dispatch(command::atom::MoveWindowToMonitor{ win, 1 });

    // Window should land on monitor 1's active workspace (1)
    int target_ws = h->core.active_workspace_on_monitor(1);
    EXPECT_EQ(h->core.workspace_of_window(win), target_ws);
}

// ---------------------------------------------------------------------------
// Fullscreen on one monitor, focus on another — stacking
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, FullscreenOnOneMonitorDoesNotAffectOther) {
    auto     h = make_dual();

    WindowId w1 = h->map_window(0x1000, 0);
    WindowId w2 = h->map_window(0x2000, 1);

    h->core.dispatch(command::atom::SetWindowFullscreen{ w1, true });

    // ws 0 should be fullscreen mode
    EXPECT_EQ(h->core.workspace_states()[0].mode, WorkspaceMode::Fullscreen);
    // ws 1 should stay normal
    EXPECT_EQ(h->core.workspace_states()[1].mode, WorkspaceMode::Normal);

    (void)w2;
}

// ---------------------------------------------------------------------------
// FocusNext/Prev only cycles within current workspace
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, FocusNextStaysOnCurrentWorkspace) {
    auto     h = make_dual();

    WindowId w1 = h->map_window(0x1000, 0);
    WindowId w2 = h->map_window(0x2000, 0);
    WindowId w3 = h->map_window(0x3000, 1); // different workspace

    h->core.dispatch(command::atom::FocusMonitor{ 0 });
    h->core.dispatch(command::atom::FocusWindow{ w1 });
    drain(h->core);

    h->core.dispatch(command::composite::FocusNextWindow{});
    auto effects = drain(h->core);

    // Focus should stay on ws 0 windows (w1 or w2), not jump to w3
    for (const auto& e : effects) {
        if (e.kind == BackendEffectKind::FocusWindow) {
            EXPECT_TRUE(e.window == w1 || e.window == w2);
            EXPECT_NE(e.window, w3);
        }
    }
}

// ---------------------------------------------------------------------------
// Multiple fullscreen windows on different monitors
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, FullscreenOnBothMonitors) {
    auto     h = make_dual();

    WindowId w1 = h->map_window(0x1000, 0);
    WindowId w2 = h->map_window(0x2000, 1);

    h->core.dispatch(command::atom::SetWindowFullscreen{ w1, true });
    h->core.dispatch(command::atom::SetWindowFullscreen{ w2, true });

    EXPECT_EQ(h->core.workspace_states()[0].mode, WorkspaceMode::Fullscreen);
    EXPECT_EQ(h->core.workspace_states()[1].mode, WorkspaceMode::Fullscreen);

    // Each should be pinned to its own monitor
    auto s1 = h->core.window_state_any(w1);
    auto s2 = h->core.window_state_any(w2);
    EXPECT_EQ(s1->pos().x(), 0);
    EXPECT_EQ(s1->size().x(), 1920);
    EXPECT_EQ(s2->pos().x(), 1920);
    EXPECT_EQ(s2->size().x(), 2560);
}

// ---------------------------------------------------------------------------
// Remove window from other monitor doesn't affect current
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, RemoveWindowOnOtherMonitorSafe) {
    auto     h = make_dual();

    WindowId w1 = h->map_window(0x1000, 0);
    WindowId w2 = h->map_window(0x2000, 1);

    h->core.dispatch(command::atom::FocusMonitor{ 0 });
    h->core.dispatch(command::atom::FocusWindow{ w1 });

    h->core.dispatch(command::atom::RemoveWindowFromAllWorkspaces{ w2 });

    // w1 should still be valid and focused
    EXPECT_NE(h->core.window_state_any(w1), nullptr);
    EXPECT_EQ(h->core.window_state_any(w2), nullptr);
}

// ---------------------------------------------------------------------------
// Borderless on multi-monitor: activated event has correct monitor index
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, BorderlessActivatedHasCorrectMonitor) {
    auto     h = make_dual();

    WindowId win = h->map_window(0x2000, 1);
    h->core.take_core_events(); // drain

    h->core.dispatch(command::atom::SetWindowBorderless{ win, true });
    auto events = h->core.take_core_events();

    bool found = false;
    for (const auto& e : events) {
        if (auto* ev = std::get_if<event::BorderlessActivated>(&e)) {
            if (ev->window == win && ev->monitor_index == 1) {
                found = true; break;
            }
        }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Insets affect tiled but not fullscreen
// ---------------------------------------------------------------------------

TEST(MultiMonitorFocus, InsetAffectsTiledNotFullscreen) {
    auto h = make_dual();

    h->core.dispatch(command::atom::ApplyMonitorTopInset{ 30 });

    WindowId tiled = h->map_window(0x1000, 0);
    WindowId fs    = h->map_window(0x2000, 0);
    h->core.dispatch(command::atom::SetWindowFullscreen{ fs, true });

    auto st = h->core.window_state_any(tiled);
    auto sf = h->core.window_state_any(fs);

    // Tiled window should start at y >= 30 (below inset)
    EXPECT_GE(st->pos().y(), 30);

    // Fullscreen should start at y = 0 (ignores inset)
    EXPECT_EQ(sf->pos().y(), 0);
    EXPECT_EQ(sf->size().y(), 1080);
}
