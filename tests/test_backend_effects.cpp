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

// ---------------------------------------------------------------------------
// MapWindow emits MapWindow effect
// ---------------------------------------------------------------------------

TEST(BackendEffects, MapWindowEmitsMapEffect) {
    TestHarness h;
    h.start();

    // MapWindow command (not SetWindowMapped) emits the MapWindow effect.
    h.core.dispatch(command::EnsureWindow{ 0x1000, 0 });
    h.core.dispatch(command::MapWindow{ 0x1000 });

    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::MapWindow, 0x1000));
}

TEST(BackendEffects, UnmapWindowEmitsUnmapEffect) {
    TestHarness h;
    h.start();

    // UnmapWindow command emits the UnmapWindow effect.
    h.core.dispatch(command::EnsureWindow{ 0x1000, 0 });
    h.core.dispatch(command::MapWindow{ 0x1000 });
    drain(h.core); // discard map effects

    h.core.dispatch(command::UnmapWindow{ 0x1000 });

    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::UnmapWindow, 0x1000));
}

// ---------------------------------------------------------------------------
// FocusNextWindow / FocusPrevWindow emit FocusWindow effect
// FocusWindow command updates wsman state (no direct effect — sync via reconcile)
// ---------------------------------------------------------------------------

TEST(BackendEffects, FocusNextWindowEmitsFocusEffect) {
    TestHarness h;
    h.start();

    h.map_window(0x1001, 0);
    h.map_window(0x1002, 0);
    drain(h.core);

    h.core.dispatch(command::FocusNextWindow{});
    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::FocusWindow));
}

TEST(BackendEffects, FocusPrevWindowEmitsFocusEffect) {
    TestHarness h;
    h.start();

    h.map_window(0x1001, 0);
    h.map_window(0x1002, 0);
    drain(h.core);

    h.core.dispatch(command::FocusPrevWindow{});
    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::FocusWindow));
}

// ---------------------------------------------------------------------------
// SwitchWorkspace hides windows on old workspace (UnmapWindow)
// and shows windows on new workspace (MapWindow)
// ---------------------------------------------------------------------------

TEST(BackendEffects, WorkspaceSwitchHidesOldWindows) {
    TestHarness h;
    h.start();

    // ws 0 is active; place a window there
    WindowId win = h.map_window(0x1000, 0);
    drain(h.core);

    // Switch to ws 1 — win should be unmapped
    h.core.dispatch(command::SwitchWorkspace{ 1, std::nullopt });
    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::UnmapWindow, win));
}

TEST(BackendEffects, WorkspaceSwitchShowsNewWindows) {
    TestHarness h;
    h.start();

    // Put a window on ws 1
    WindowId win = h.map_window(0x1000, 1);
    // Switch to ws 0 first so ws 1 is inactive
    h.core.dispatch(command::SwitchWorkspace{ 0, std::nullopt });
    drain(h.core);

    // Switch to ws 1 — win should be mapped
    h.core.dispatch(command::SwitchWorkspace{ 1, std::nullopt });
    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::MapWindow, win));
}

// ---------------------------------------------------------------------------
// FocusRoot: emitted when switching to an empty workspace
// ---------------------------------------------------------------------------

TEST(BackendEffects, FocusRootWhenSwitchingToEmptyWorkspace) {
    TestHarness h;
    h.start();

    // ws 0 has a window; ws 1 is empty.
    h.map_window(0x1000, 0);
    drain(h.core);

    // Switch to ws 1 (empty) — sync_current_focus will emit FocusRoot.
    h.core.dispatch(command::SwitchWorkspace{ 1, std::nullopt });
    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::FocusRoot));
}

// ---------------------------------------------------------------------------
// UpdateWindow after geometry change
// ---------------------------------------------------------------------------

TEST(BackendEffects, SetWindowGeometryEmitsUpdateEffect) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    drain(h.core);

    h.core.dispatch(command::SetWindowGeometry{ win, 100, 200, 640, 480 });
    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::UpdateWindow, win));
}

// ---------------------------------------------------------------------------
// MoveWindowToWorkspace: hidden when target ws is inactive
// ---------------------------------------------------------------------------

TEST(BackendEffects, MoveToInactiveWorkspaceEmitsUnmap) {
    TestHarness h;
    h.start();

    // ws 0 active, move window to ws 1 (inactive)
    WindowId win = h.map_window(0x1000, 0);
    drain(h.core);

    h.core.dispatch(command::MoveWindowToWorkspace{ win, 1 });
    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::UnmapWindow, win));
}

TEST(BackendEffects, MoveToActiveWorkspaceEmitsMap) {
    TestHarness h;
    h.start();

    // Put window on ws 1 (inactive), switch there, then move to ws 0
    WindowId win = h.map_window(0x1000, 1);
    drain(h.core);

    // Switch to ws 1 so the window becomes visible
    h.core.dispatch(command::SwitchWorkspace{ 1, std::nullopt });
    drain(h.core);

    // Move back to ws 0 which is now inactive — window gets unmapped
    h.core.dispatch(command::MoveWindowToWorkspace{ win, 0 });
    auto fx = drain(h.core);
    // ws 0 is inactive from this monitor's perspective, window is unmapped
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::UnmapWindow, win));
}

// ---------------------------------------------------------------------------
// Fullscreen: UpdateWindow emitted when toggled
// ---------------------------------------------------------------------------

TEST(BackendEffects, FullscreenEmitsUpdate) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    drain(h.core);

    h.core.dispatch(command::SetWindowFullscreen{ win, true });
    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::UpdateWindow, win));

    drain(h.core);
    h.core.dispatch(command::SetWindowFullscreen{ win, false });
    fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::UpdateWindow, win));
}

// ---------------------------------------------------------------------------
// Multiple windows: effects are per-window
// ---------------------------------------------------------------------------

TEST(BackendEffects, EffectsArePerWindow) {
    TestHarness h;
    h.start();

    WindowId a = h.map_window(0x2001, 0);
    WindowId b = h.map_window(0x2002, 0);
    drain(h.core);

    h.core.dispatch(command::SetWindowGeometry{ a, 0, 0, 320, 240 });
    auto fx = drain(h.core);
    EXPECT_TRUE(has_effect(fx, BackendEffectKind::UpdateWindow, a));
    // b should NOT have an effect here
    EXPECT_FALSE(has_effect(fx, BackendEffectKind::UpdateWindow, b));
}
