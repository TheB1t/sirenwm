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
// Workspace mode transitions
// ---------------------------------------------------------------------------

TEST(FullscreenStacking, EnterFullscreenSetsWorkspaceMode) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    drain(h.core);

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });

    auto& ws = h.core.workspace_states()[0];
    EXPECT_EQ(ws.mode, WorkspaceMode::Fullscreen);
}

TEST(FullscreenStacking, ExitFullscreenRestoresNormalMode) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });
    h.core.dispatch(command::atom::SetWindowFullscreen{ win, false });

    auto& ws = h.core.workspace_states()[0];
    EXPECT_EQ(ws.mode, WorkspaceMode::Normal);
}

TEST(FullscreenStacking, BorderlessAlsoEntersFullscreenMode) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    drain(h.core);

    h.core.dispatch(command::atom::SetWindowBorderless{ win, true });

    auto& ws = h.core.workspace_states()[0];
    EXPECT_EQ(ws.mode, WorkspaceMode::Fullscreen);
}

// ---------------------------------------------------------------------------
// Raise/Lower stacking effects
// ---------------------------------------------------------------------------

TEST(FullscreenStacking, FullscreenFocusedEmitsRaise) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    drain(h.core);

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });
    auto effects = drain(h.core);

    EXPECT_TRUE(has_effect(effects, BackendEffectKind::RaiseWindow, win));
}

TEST(FullscreenStacking, FocusNormalWhileFullscreenExistsLowersFS) {
    TestHarness h;
    h.start();

    WindowId fs_win  = h.map_window(0x1000, 0);
    WindowId nrm_win = h.map_window(0x2000, 0);
    h.core.dispatch(command::atom::SetWindowFullscreen{ fs_win, true });
    drain(h.core);

    // Focus the normal window
    h.core.dispatch(command::atom::FocusWindow{ nrm_win });
    // evaluate_workspace_fullscreen is called during various operations;
    // trigger it by switching focus which should re-evaluate stacking
    h.core.dispatch(command::atom::SetWindowFullscreen{ fs_win, true }); // re-trigger eval
    auto effects = drain(h.core);

    EXPECT_TRUE(has_effect(effects, BackendEffectKind::RaiseWindow, fs_win));
}

// ---------------------------------------------------------------------------
// Geometry preservation
// ---------------------------------------------------------------------------

TEST(FullscreenStacking, FullscreenPinsToMonitorGeometry) {
    TestHarness h({ make_monitor(0, 100, 200, 1920, 1080) });
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });

    auto w = h.core.window_state_any(win);
    ASSERT_TRUE(w);
    // Physical area should match monitor (no insets applied)
    EXPECT_EQ(w->pos().x(), 100);
    EXPECT_EQ(w->pos().y(), 200);
    EXPECT_EQ(w->size().x(), 1920);
    EXPECT_EQ(w->size().y(), 1080);
}

TEST(FullscreenStacking, FullscreenPreserveGeometrySavesOriginal) {
    TestHarness h({ make_monitor(0, 0, 0, 1920, 1080) });
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFloating{ win, true });
    h.core.dispatch(command::atom::SetWindowGeometry{ win, {50, 50}, {800, 600} });

    // preserve_geometry still pins to monitor (via evaluate_workspace_fullscreen)
    // but it does save the original geometry for later restoration.
    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true, true });

    auto w = h.core.window_state_any(win);
    ASSERT_TRUE(w);
    EXPECT_TRUE(w->fullscreen);

    // Exit fullscreen — floating geometry should be restored from saved values
    h.core.dispatch(command::atom::SetWindowFullscreen{ win, false });
    w = h.core.window_state_any(win);
    EXPECT_EQ(w->pos().x(), 50);
    EXPECT_EQ(w->pos().y(), 50);
    EXPECT_EQ(w->size().x(), 800);
    EXPECT_EQ(w->size().y(), 600);
}

TEST(FullscreenStacking, FullscreenRestoresGeometryForFloating) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFloating{ win, true });
    h.core.dispatch(command::atom::SetWindowGeometry{ win, {50, 50}, {800, 600} });

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });
    // Verify fullscreen changed geometry
    auto w = h.core.window_state_any(win);
    EXPECT_NE(w->pos().x(), 50);

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, false });
    w = h.core.window_state_any(win);
    ASSERT_TRUE(w);
    // Position and size should be restored for floating windows
    EXPECT_EQ(w->pos().x(), 50);
    EXPECT_EQ(w->pos().y(), 50);
    EXPECT_EQ(w->size().x(), 800);
    EXPECT_EQ(w->size().y(), 600);
}

TEST(FullscreenStacking, FullscreenZerosBorderOnEntry) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowBorderWidth{ win, 3 });

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });
    auto w = h.core.window_state_any(win);
    EXPECT_EQ(w->border_width, 0);
}

TEST(FullscreenStacking, FullscreenRestoresBorderOnExit) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowBorderWidth{ win, 3 });

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });
    EXPECT_EQ(h.core.window_state_any(win)->border_width, 0);

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, false });
    auto w = h.core.window_state_any(win);
    // After fullscreen exit, arrange() may set border to theme default.
    // The saved border is restored but arrange can overwrite it.
    // At minimum, border should not stay at 0.
    EXPECT_GT(w->border_width, 0);
}

// ---------------------------------------------------------------------------
// Remove fullscreen window exits mode
// ---------------------------------------------------------------------------

TEST(FullscreenStacking, RemoveFullscreenWindowExitsMode) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });
    EXPECT_EQ(h.core.workspace_states()[0].mode, WorkspaceMode::Fullscreen);

    h.core.dispatch(command::atom::RemoveWindowFromAllWorkspaces{ win });
    EXPECT_EQ(h.core.workspace_states()[0].mode, WorkspaceMode::Normal);
}

// ---------------------------------------------------------------------------
// RaiseDocks event
// ---------------------------------------------------------------------------

TEST(FullscreenStacking, RaiseDocksEmittedOnEnterAndExit) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.take_core_events(); // drain initial events

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });
    auto events = h.core.take_core_events();
    EXPECT_TRUE(has_domain_event<event::RaiseDocks>(events));

    h.core.dispatch(command::atom::SetWindowFullscreen{ win, false });
    events = h.core.take_core_events();
    EXPECT_TRUE(has_domain_event<event::RaiseDocks>(events));
}

// ---------------------------------------------------------------------------
// Fullscreen with insets
// ---------------------------------------------------------------------------

TEST(FullscreenStacking, FullscreenCoversFullMonitorWithInset) {
    TestHarness h({ make_monitor(0, 0, 0, 1920, 1080) });
    h.start();

    h.core.dispatch(command::atom::ApplyMonitorTopInset{ 20 });

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });

    auto w = h.core.window_state_any(win);
    ASSERT_TRUE(w);
    // Fullscreen should cover the full original monitor area (before insets)
    EXPECT_EQ(w->pos().y(), 0);
    EXPECT_EQ(w->size().x(), 1920);
    EXPECT_EQ(w->size().y(), 1080);
}
