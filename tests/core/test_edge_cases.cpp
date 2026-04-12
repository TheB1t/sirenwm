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

template<typename Ev>
static bool has_domain_event(const std::vector<CoreDomainEvent>& events) {
    for (const auto& e : events)
        if (std::holds_alternative<Ev>(e))
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// MoveFocusedWindowToWorkspace
// ---------------------------------------------------------------------------

TEST(EdgeCases, MoveFocusedWindowToWorkspace) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::FocusWindow{ win });

    bool ok = h.core.dispatch(command::composite::MoveFocusedWindowToWorkspace{ 1 });
    EXPECT_TRUE(ok);
    EXPECT_EQ(h.core.workspace_of_window(win), 1);
}

TEST(EdgeCases, MoveFocusedWindowNoFocusReturnsFalse) {
    TestHarness h;
    h.start();

    // No windows at all — nothing focused
    bool ok = h.core.dispatch(command::composite::MoveFocusedWindowToWorkspace{ 1 });
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// AssignWindowWorkspace
// ---------------------------------------------------------------------------

TEST(EdgeCases, AssignWindowWorkspaceBasic) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.take_core_events(); // drain

    bool ok = h.core.dispatch(command::atom::AssignWindowWorkspace{ win, 2 });
    EXPECT_TRUE(ok);

    auto events = h.take_core_events();
    bool found  = false;
    for (const auto& e : events) {
        if (auto* ev = std::get_if<event::WindowAssignedToWorkspace>(&e)) {
            if (ev->window == win && ev->workspace_id == 2) {
                found = true; break;
            }
        }
    }
    EXPECT_TRUE(found);
}

TEST(EdgeCases, AssignWindowWorkspaceNonexistent) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::AssignWindowWorkspace{ 0xDEAD, 1 });
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// SetWindowHiddenByWorkspace
// ---------------------------------------------------------------------------

TEST(EdgeCases, SetWindowHiddenByWorkspace) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowHiddenByWorkspace{ win, true });

    auto w = h.core.window_state_any(win);
    EXPECT_FALSE(w->is_visible());

    h.core.dispatch(command::atom::SetWindowHiddenByWorkspace{ win, false });
    w = h.core.window_state_any(win);
    EXPECT_TRUE(w->is_visible());
}

// ---------------------------------------------------------------------------
// Map/Unmap idempotency
// ---------------------------------------------------------------------------

TEST(EdgeCases, DoubleMapDoesNotDuplicate) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    drain(h.core);

    // Map again
    h.core.dispatch(command::atom::MapWindow{ win });
    auto effects = drain(h.core);

    // Should NOT emit another MapWindow effect (already mapped)
    EXPECT_FALSE(has_effect(effects, BackendEffectKind::MapWindow, win));
}

TEST(EdgeCases, DoubleUnmapDoesNotDuplicate) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::UnmapWindow{ win });
    drain(h.core);

    // Unmap again
    h.core.dispatch(command::atom::UnmapWindow{ win });
    auto effects = drain(h.core);

    // Should NOT emit another UnmapWindow effect (already unmapped)
    EXPECT_FALSE(has_effect(effects, BackendEffectKind::UnmapWindow, win));
}

// ---------------------------------------------------------------------------
// Fullscreen edge cases
// ---------------------------------------------------------------------------

TEST(EdgeCases, FullscreenDisableOnNonFullscreenIsNoop) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFloating{ win, true });
    h.core.dispatch(command::atom::SetWindowGeometry{ win, {50, 50}, {400, 300} });

    // Disable fullscreen when it's not enabled — should be harmless
    h.core.dispatch(command::atom::SetWindowFullscreen{ win, false });

    auto w = h.core.window_state_any(win);
    EXPECT_FALSE(w->fullscreen);
    // Geometry should be unchanged
    EXPECT_EQ(w->pos().x(), 50);
    EXPECT_EQ(w->size().x(), 400);
}

TEST(EdgeCases, FullscreenDoubleEnableDoesNotLoseOriginalGeometry) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFloating{ win, true });
    h.core.dispatch(command::atom::SetWindowGeometry{ win, {50, 50}, {400, 300} });

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });
    // Enable fullscreen again — should NOT overwrite saved pre-fullscreen geometry
    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, false });
    auto w = h.core.window_state_any(win);
    EXPECT_EQ(w->pos().x(), 50);
    EXPECT_EQ(w->pos().y(), 50);
    EXPECT_EQ(w->size().x(), 400);
    EXPECT_EQ(w->size().y(), 300);
}

TEST(EdgeCases, FullscreenNonexistentWindowReturnsFalse) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::SetWindowFullscreen{ 0xDEAD, true });
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// Borderless edge cases
// ---------------------------------------------------------------------------

TEST(EdgeCases, BorderlessZerosBorderOnNonSelfManaged) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowBorderWidth{ win, 3 });

    h.core.dispatch(command::atom::SetWindowBorderless{ win, true });
    auto w = h.core.window_state_any(win);
    EXPECT_EQ(w->border_width, 0);
}

// ---------------------------------------------------------------------------
// EnsureWindow
// ---------------------------------------------------------------------------

TEST(EdgeCases, EnsureWindowCreatesNew) {
    TestHarness h;
    h.start();

    WindowId win = 0xABCD;
    bool     ok  = h.core.dispatch(command::atom::EnsureWindow{ win, 0 });
    EXPECT_TRUE(ok);
    EXPECT_NE(h.core.window_state_any(win), nullptr);
}

TEST(EdgeCases, EnsureWindowMovesExisting) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    EXPECT_EQ(h.core.workspace_of_window(win), 0);

    h.core.dispatch(command::atom::EnsureWindow{ win, 1 });
    EXPECT_EQ(h.core.workspace_of_window(win), 1);
}

TEST(EdgeCases, EnsureWindowNoWindowReturnsFalse) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::EnsureWindow{ NO_WINDOW, 0 });
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// RemoveWindowFromAllWorkspaces
// ---------------------------------------------------------------------------

TEST(EdgeCases, RemoveWindowCleansUp) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    EXPECT_NE(h.core.window_state_any(win), nullptr);

    h.core.dispatch(command::atom::RemoveWindowFromAllWorkspaces{ win });
    EXPECT_EQ(h.core.window_state_any(win), nullptr);
}

TEST(EdgeCases, RemoveWindowTwiceIsSafe) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::RemoveWindowFromAllWorkspaces{ win });
    // Second remove should not crash
    h.core.dispatch(command::atom::RemoveWindowFromAllWorkspaces{ win });
    EXPECT_EQ(h.core.window_state_any(win), nullptr);
}

// ---------------------------------------------------------------------------
// FocusMonitor edge cases
// ---------------------------------------------------------------------------

TEST(EdgeCases, FocusMonitorInvalidReturnsFalse) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::FocusMonitor{ 99 });
    EXPECT_FALSE(ok);
}

TEST(EdgeCases, FocusMonitorEmitsWarpPointer) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    drain(h.core);
    h.core.dispatch(command::atom::FocusMonitor{ 1 });
    auto effects = drain(h.core);

    EXPECT_TRUE(has_effect(effects, BackendEffectKind::WarpPointer));
}

TEST(EdgeCases, FocusMonitorRestoresLastFocusedWindow) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    CoreSettings s;
    s.workspace_defs = { { "[1]", "primary" }, { "[2]", "secondary" } };
    h.core.apply_settings(s);
    h.start();

    WindowId w1 = h.map_window(0x1000, 0);
    WindowId w2 = h.map_window(0x2000, 1);
    h.core.dispatch(command::atom::FocusWindow{ w1 });
    h.core.dispatch(command::atom::FocusMonitor{ 1 });
    h.core.dispatch(command::atom::FocusWindow{ w2 });

    // Switch back to monitor 0
    drain(h.core);
    h.core.dispatch(command::atom::FocusMonitor{ 0 });
    auto effects = drain(h.core);

    // Should emit FocusWindow for w1 (the last focused window on monitor 0)
    EXPECT_TRUE(has_effect(effects, BackendEffectKind::FocusWindow, w1));
}

// ---------------------------------------------------------------------------
// SetWindowFloating / ToggleWindowFloating on nonexistent window
// ---------------------------------------------------------------------------

TEST(EdgeCases, SetFloatingNonexistentReturnsFalse) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::SetWindowFloating{ 0xDEAD, true });
    EXPECT_FALSE(ok);
}

TEST(EdgeCases, ToggleFloatingNonexistentReturnsFalse) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::composite::ToggleWindowFloating{ 0xDEAD });
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// SetWindowPosition / SetWindowSize on nonexistent
// ---------------------------------------------------------------------------

TEST(EdgeCases, SetPositionNonexistentReturnsFalse) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::SetWindowPosition{ 0xDEAD, {0, 0} });
    EXPECT_FALSE(ok);
}

TEST(EdgeCases, SetSizeNonexistentReturnsFalse) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::SetWindowSize{ 0xDEAD, {100, 100} });
    EXPECT_FALSE(ok);
}

TEST(EdgeCases, SetGeometryNonexistentReturnsFalse) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::SetWindowGeometry{ 0xDEAD, {0, 0}, {100, 100} });
    EXPECT_FALSE(ok);
}

TEST(EdgeCases, SetBorderWidthNonexistentReturnsFalse) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::SetWindowBorderWidth{ 0xDEAD, 5 });
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// Workspace switch to same workspace is idempotent
// ---------------------------------------------------------------------------

TEST(EdgeCases, SwitchToSameWorkspaceDoesNotCrash) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.take_core_events(); // drain

    // Switching to same workspace should not crash; event may or may not fire
    h.core.dispatch(command::atom::SwitchWorkspace{ 0, std::nullopt });
    // Window should still be visible and valid
    EXPECT_NE(h.core.window_state_any(win), nullptr);
    EXPECT_TRUE(h.core.window_state_any(win)->is_visible());
}

// ---------------------------------------------------------------------------
// MoveWindowToWorkspace to same workspace
// ---------------------------------------------------------------------------

TEST(EdgeCases, MoveWindowToSameWorkspace) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::MoveWindowToWorkspace{ win, 0 });
    // Should still be on workspace 0, no crash
    EXPECT_EQ(h.core.workspace_of_window(win), 0);
}

// ---------------------------------------------------------------------------
// Multiple windows on workspace: layout consistency
// ---------------------------------------------------------------------------

TEST(EdgeCases, MultipleWindowsTrackedCorrectly) {
    TestHarness h({ make_monitor(0, 0, 0, 1920, 1080) });
    h.start();

    WindowId w1 = h.map_window(0x1000, 0);
    WindowId w2 = h.map_window(0x2000, 0);
    WindowId w3 = h.map_window(0x3000, 0);

    // All windows should be tracked and on workspace 0
    EXPECT_NE(h.core.window_state_any(w1), nullptr);
    EXPECT_NE(h.core.window_state_any(w2), nullptr);
    EXPECT_NE(h.core.window_state_any(w3), nullptr);
    EXPECT_EQ(h.core.workspace_of_window(w1), 0);
    EXPECT_EQ(h.core.workspace_of_window(w2), 0);
    EXPECT_EQ(h.core.workspace_of_window(w3), 0);
}

// ---------------------------------------------------------------------------
// Map on invisible workspace does NOT emit MapWindow effect
// ---------------------------------------------------------------------------

TEST(EdgeCases, MapOnInvisibleWorkspaceNoMapEffect) {
    TestHarness h;
    h.start();

    // ws 0 is active; create window on ws 1 (not visible)
    h.core.dispatch(command::atom::EnsureWindow{ 0xABCD, 1 });
    drain(h.core);

    h.core.dispatch(command::atom::SetWindowMapped{ 0xABCD, true });
    auto effects = drain(h.core);

    // Window is on invisible workspace, so MapWindow effect should NOT fire
    EXPECT_FALSE(has_effect(effects, BackendEffectKind::MapWindow, 0xABCD));
}

// ---------------------------------------------------------------------------
// SetWindowMetadata on nonexistent window
// ---------------------------------------------------------------------------

TEST(EdgeCases, SetMetadataNonexistentReturnsFalse) {
    TestHarness h;
    h.start();

    command::atom::SetWindowMetadata meta;
    meta.window = 0xDEAD;
    bool                       ok = h.core.dispatch(meta);
    EXPECT_FALSE(ok);
}
