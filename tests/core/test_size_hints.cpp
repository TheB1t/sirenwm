#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <core.hpp>

#include "test_harness.hpp"

// ---------------------------------------------------------------------------
// apply_size_hints — clamp and increment grid snapping
// ---------------------------------------------------------------------------

TEST(SizeHints, ClampToMinMax) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFloating{ win, true });

    // Set hints: min=200x100, max=800x600
    command::atom::SetWindowMetadata meta;
    meta.window         = win;
    meta.hints.size_min = {200, 100};
    meta.hints.size_max = {800, 600};
    h.core.dispatch(meta);

    // Request too small
    h.core.dispatch(command::atom::SetWindowSize{ win, {50, 50} });
    auto w = h.core.window_state_any(win);
    EXPECT_GE(w->size().x(), 200);
    EXPECT_GE(w->size().y(), 100);

    // Request too large
    h.core.dispatch(command::atom::SetWindowSize{ win, {2000, 2000} });
    w = h.core.window_state_any(win);
    EXPECT_LE(w->size().x(), 800);
    EXPECT_LE(w->size().y(), 600);
}

TEST(SizeHints, IncrementSnapping) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFloating{ win, true });

    command::atom::SetWindowMetadata meta;
    meta.window          = win;
    meta.hints.size_base = {5, 5};
    meta.hints.size_inc  = {10, 10};
    h.core.dispatch(meta);

    // 123 should snap to 5 + 11*10 = 115 (floor to grid)
    h.core.dispatch(command::atom::SetWindowSize{ win, {123, 123} });
    auto w = h.core.window_state_any(win);
    EXPECT_EQ(w->size().x(), 115);
    EXPECT_EQ(w->size().y(), 115);
}

TEST(SizeHints, IncrementSnappingWithMinSize) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFloating{ win, true });

    command::atom::SetWindowMetadata meta;
    meta.window          = win;
    meta.hints.size_min  = {100, 100};
    meta.hints.size_inc  = {10, 10};
    meta.hints.size_base = {0, 0};
    h.core.dispatch(meta);

    // Request 105 — base is min (100), snap to 100 + 0*10 = 100
    h.core.dispatch(command::atom::SetWindowSize{ win, {105, 105} });
    auto w = h.core.window_state_any(win);
    EXPECT_EQ(w->size().x(), 100);
    EXPECT_GE(w->size().y(), 100);
}

TEST(SizeHints, TiledWindowIgnoresSizeHints) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    // Window is tiled (not floating)

    command::atom::SetWindowMetadata meta;
    meta.window         = win;
    meta.hints.size_min = {500, 500};
    meta.hints.size_max = {500, 500};
    h.core.dispatch(meta);

    // SetWindowSize on tiled windows does NOT apply hints
    h.core.dispatch(command::atom::SetWindowSize{ win, {200, 200} });
    auto w = h.core.window_state_any(win);
    EXPECT_EQ(w->size().x(), 200);
    EXPECT_EQ(w->size().y(), 200);
}

TEST(SizeHints, SetWindowGeometryAppliesHintsForFloating) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFloating{ win, true });

    command::atom::SetWindowMetadata meta;
    meta.window         = win;
    meta.hints.size_min = {300, 300};
    meta.hints.size_max = {300, 300};
    h.core.dispatch(meta);

    h.core.dispatch(command::atom::SetWindowGeometry{ win, {10, 10}, {100, 100} });
    auto w = h.core.window_state_any(win);
    EXPECT_EQ(w->size().x(), 300);
    EXPECT_EQ(w->size().y(), 300);
    // Position is set regardless
    EXPECT_EQ(w->pos().x(), 10);
}

// ---------------------------------------------------------------------------
// Transient window routing
// ---------------------------------------------------------------------------

TEST(SizeHints, TransientRoutesToParentWorkspace) {
    TestHarness h({
        make_monitor(0, 0, 0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    CoreSettings s;
    s.workspace_defs = { { "[1]", "primary" }, { "[2]", "secondary" } };
    h.core.apply_settings(s);
    h.start();

    WindowId                         parent = h.map_window(0x1000, 0);
    WindowId                         child  = h.map_window(0x2000, 1);

    command::atom::SetWindowMetadata meta;
    meta.window        = child;
    meta.transient_for = parent;
    h.core.dispatch(meta);

    // Child should have been moved to parent's workspace
    EXPECT_EQ(h.core.workspace_of_window(child), 0);
    // Child should be floating
    auto w = h.core.window_state_any(child);
    EXPECT_TRUE(w->floating);
}

TEST(SizeHints, TransientSuppressesFocusOnce) {
    TestHarness h;
    h.start();

    WindowId                         parent = h.map_window(0x1000, 0);
    WindowId                         child  = h.map_window(0x2000, 0);

    command::atom::SetWindowMetadata meta;
    meta.window        = child;
    meta.transient_for = parent;
    h.core.dispatch(meta);

    auto w = h.core.window_state_any(child);
    EXPECT_TRUE(w->suppress_focus_once);
}

// ---------------------------------------------------------------------------
// Auto-float classification
// ---------------------------------------------------------------------------

TEST(SizeHints, DialogAutoFloats) {
    TestHarness h;
    h.start();

    WindowId                         win = h.map_window(0x1000, 0);

    command::atom::SetWindowMetadata meta;
    meta.window = win;
    meta.type   = WindowType::Dialog;
    h.core.dispatch(meta);

    auto w = h.core.window_state_any(win);
    EXPECT_TRUE(w->floating);
}

TEST(SizeHints, UtilityAutoFloats) {
    TestHarness h;
    h.start();

    WindowId                         win = h.map_window(0x1000, 0);

    command::atom::SetWindowMetadata meta;
    meta.window = win;
    meta.type   = WindowType::Utility;
    h.core.dispatch(meta);

    auto w = h.core.window_state_any(win);
    EXPECT_TRUE(w->floating);
}

TEST(SizeHints, FixedSizeNonBorderlessAutoFloats) {
    TestHarness h;
    h.start();

    WindowId                         win = h.map_window(0x1000, 0);

    command::atom::SetWindowMetadata meta;
    meta.window           = win;
    meta.hints.fixed_size = true;
    h.core.dispatch(meta);

    auto w = h.core.window_state_any(win);
    EXPECT_TRUE(w->floating);
}

TEST(SizeHints, SplashAutoFloats) {
    TestHarness h;
    h.start();

    WindowId                         win = h.map_window(0x1000, 0);

    command::atom::SetWindowMetadata meta;
    meta.window = win;
    meta.type   = WindowType::Splash;
    h.core.dispatch(meta);

    auto w = h.core.window_state_any(win);
    EXPECT_TRUE(w->floating);
}
