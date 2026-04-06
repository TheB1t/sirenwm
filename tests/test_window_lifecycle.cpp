#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <core.hpp>

#include "test_harness.hpp"

// ---------------------------------------------------------------------------
// Basic lifecycle: EnsureWindow → SetWindowMetadata → SetWindowMapped
// ---------------------------------------------------------------------------

TEST(WindowLifecycle, NewWindowIsTracked) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x1000, 0 });
    EXPECT_NE(h.core.window_state_any(0x1000), nullptr);
}

TEST(WindowLifecycle, MetadataStoredCorrectly) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x2000, 0 });

    command::SetWindowMetadata meta;
    meta.window      = 0x2000;
    meta.wm_class    = "firefox";
    meta.wm_instance = "Navigator";
    meta.type        = WindowType::Normal;
    h.core.dispatch(meta);

    auto ws = h.core.window_state_any(0x2000);
    ASSERT_NE(ws, nullptr);
    EXPECT_EQ(ws->wm_class,    "firefox");
    EXPECT_EQ(ws->wm_instance, "Navigator");
}

TEST(WindowLifecycle, MappedWindowIsVisible) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x3000, 0 });
    h.core.dispatch(command::SetWindowMapped{ 0x3000, true });

    auto ws = h.core.window_state_any(0x3000);
    ASSERT_NE(ws, nullptr);
    EXPECT_TRUE(ws->mapped);
    EXPECT_TRUE(ws->is_visible());
}

TEST(WindowLifecycle, UnmappedWindowIsNotVisible) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x3000, 0 });
    h.core.dispatch(command::SetWindowMapped{ 0x3000, true });
    h.core.dispatch(command::SetWindowMapped{ 0x3000, false });

    auto ws = h.core.window_state_any(0x3000);
    ASSERT_NE(ws, nullptr);
    EXPECT_FALSE(ws->mapped);
    EXPECT_FALSE(ws->is_visible());
}

TEST(WindowLifecycle, RemoveWindowCleansUp) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x4000, 0 });
    h.core.dispatch(command::SetWindowMapped{ 0x4000, true });
    h.core.dispatch(command::RemoveWindowFromAllWorkspaces{ 0x4000 });

    EXPECT_EQ(h.core.window_state_any(0x4000), nullptr);
}

// ---------------------------------------------------------------------------
// Hints: fixed-size → auto-float
// ---------------------------------------------------------------------------

TEST(WindowLifecycle, FixedSizeHintSetsFlag) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x5000, 0 });

    command::SetWindowMetadata meta;
    meta.window          = 0x5000;
    meta.hints.fixed_size = true;
    h.core.dispatch(meta);

    auto ws = h.core.window_state_any(0x5000);
    ASSERT_NE(ws, nullptr);
    EXPECT_TRUE(ws->size_locked);
}

TEST(WindowLifecycle, NoInputFocusHintSetsFlag) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x5001, 0 });

    command::SetWindowMetadata meta;
    meta.window            = 0x5001;
    meta.hints.never_focus = true;
    h.core.dispatch(meta);

    auto ws = h.core.window_state_any(0x5001);
    ASSERT_NE(ws, nullptr);
    EXPECT_TRUE(ws->no_input_focus);
}

TEST(WindowLifecycle, StaticGravityHintSetsPreservePosition) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x5002, 0 });

    command::SetWindowMetadata meta;
    meta.window               = 0x5002;
    meta.hints.static_gravity = true;
    h.core.dispatch(meta);

    auto ws = h.core.window_state_any(0x5002);
    ASSERT_NE(ws, nullptr);
    EXPECT_TRUE(ws->preserve_position);
}

// ---------------------------------------------------------------------------
// Dialog type
// ---------------------------------------------------------------------------

TEST(WindowLifecycle, DialogTypeIsRecognized) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x6000, 0 });

    command::SetWindowMetadata meta;
    meta.window = 0x6000;
    meta.type   = WindowType::Dialog;
    h.core.dispatch(meta);

    auto ws = h.core.window_state_any(0x6000);
    ASSERT_NE(ws, nullptr);
    EXPECT_TRUE(ws->is_dialog());
}

TEST(WindowLifecycle, ModalTypeIsDialog) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x6001, 0 });

    command::SetWindowMetadata meta;
    meta.window = 0x6001;
    meta.type   = WindowType::Modal;
    h.core.dispatch(meta);

    auto ws = h.core.window_state_any(0x6001);
    ASSERT_NE(ws, nullptr);
    EXPECT_TRUE(ws->is_dialog());
}

// ---------------------------------------------------------------------------
// covers_monitor + pre_fullscreen + !is_xembed → self-managed
// ---------------------------------------------------------------------------

TEST(WindowLifecycle, CoversMonitorHintSetsSelfManaged) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x7000, 0 });

    command::SetWindowMetadata meta;
    meta.window                = 0x7000;
    meta.hints.covers_monitor  = true;
    // self_managed requires: pre_fullscreen && covers_monitor && !is_xembed
    meta.hints.pre_fullscreen  = true;
    meta.hints.is_xembed       = false;
    h.core.dispatch(meta);

    auto ws = h.core.window_state_any(0x7000);
    ASSERT_NE(ws, nullptr);
    EXPECT_TRUE(ws->self_managed);
}

TEST(WindowLifecycle, CoversMonitorWithoutPreFullscreenIsNotSelfManaged) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::EnsureWindow{ 0x7001, 0 });

    command::SetWindowMetadata meta;
    meta.window               = 0x7001;
    meta.hints.covers_monitor = true;
    meta.hints.pre_fullscreen = false; // no pre-fullscreen → wm-borderless, not self-managed
    h.core.dispatch(meta);

    auto ws = h.core.window_state_any(0x7001);
    ASSERT_NE(ws, nullptr);
    EXPECT_FALSE(ws->self_managed);
    // But it will be promoted to borderless (covers_monitor + no_decorations check)
}

// ---------------------------------------------------------------------------
// HideWindow / is_visible
// ---------------------------------------------------------------------------

TEST(WindowLifecycle, HideWindowHidesExplicitly) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x8000, 0);
    EXPECT_TRUE(h.core.window_state_any(win)->is_visible());

    h.core.dispatch(command::HideWindow{ win });
    auto ws = h.core.window_state_any(win);
    ASSERT_NE(ws, nullptr);
    EXPECT_FALSE(ws->is_visible());
    EXPECT_TRUE(ws->hidden_explicitly);
}

// ---------------------------------------------------------------------------
// SuppressFocusOnce: consumed on first access
// ---------------------------------------------------------------------------

TEST(WindowLifecycle, SuppressFocusOnceIsConsumed) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x9000, 0);
    h.core.dispatch(command::SetWindowSuppressFocusOnce{ win, true });

    EXPECT_TRUE(h.core.consume_window_suppress_focus_once(win));
    // second call: already consumed
    EXPECT_FALSE(h.core.consume_window_suppress_focus_once(win));
}

// ---------------------------------------------------------------------------
// SetWindowBorderWidth
// ---------------------------------------------------------------------------

TEST(WindowLifecycle, BorderWidthIsStored) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0xA000, 0);
    h.core.dispatch(command::SetWindowBorderWidth{ win, 3 });

    auto ws = h.core.window_state_any(win);
    ASSERT_NE(ws, nullptr);
    EXPECT_EQ(ws->border_width, 3u);
}

// ---------------------------------------------------------------------------
// Geometry commands
// ---------------------------------------------------------------------------

TEST(WindowLifecycle, SetWindowPositionUpdatesCoords) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0xB000, 0);
    h.core.dispatch(command::SetWindowPosition{ win, 42, 100 });

    auto ws = h.core.window_state_any(win);
    ASSERT_NE(ws, nullptr);
    EXPECT_EQ(ws->x, 42);
    EXPECT_EQ(ws->y, 100);
}

TEST(WindowLifecycle, SetWindowSizeUpdatesDimensions) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0xC000, 0);
    h.core.dispatch(command::SetWindowSize{ win, 800, 600 });

    auto ws = h.core.window_state_any(win);
    ASSERT_NE(ws, nullptr);
    EXPECT_EQ(ws->width,  800u);
    EXPECT_EQ(ws->height, 600u);
}

TEST(WindowLifecycle, SetWindowGeometrySetsAll) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0xD000, 0);
    h.core.dispatch(command::SetWindowGeometry{ win, 10, 20, 320, 240 });

    auto ws = h.core.window_state_any(win);
    ASSERT_NE(ws, nullptr);
    EXPECT_EQ(ws->x,      10);
    EXPECT_EQ(ws->y,      20);
    EXPECT_EQ(ws->width,  320u);
    EXPECT_EQ(ws->height, 240u);
}

// ---------------------------------------------------------------------------
// Borderless
// ---------------------------------------------------------------------------

TEST(WindowLifecycle, SetWindowBorderlessSetsFlag) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0xE000, 0);
    h.core.dispatch(command::SetWindowBorderless{ win, true });

    EXPECT_TRUE(h.core.window_state_any(win)->borderless);
}

TEST(WindowLifecycle, SetWindowBorderlessClearsFlag) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0xE001, 0);
    h.core.dispatch(command::SetWindowBorderless{ win, true });
    h.core.dispatch(command::SetWindowBorderless{ win, false });

    EXPECT_FALSE(h.core.window_state_any(win)->borderless);
}

// ---------------------------------------------------------------------------
// Fullscreen state
// ---------------------------------------------------------------------------

TEST(WindowLifecycle, FullscreenSetsFlag) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0xF000, 0);
    h.core.dispatch(command::SetWindowFullscreen{ win, true });

    EXPECT_TRUE(h.core.is_window_fullscreen(win));
}

TEST(WindowLifecycle, FullscreenClearsFlagOnDisable) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0xF001, 0);
    h.core.dispatch(command::SetWindowFullscreen{ win, true });
    h.core.dispatch(command::SetWindowFullscreen{ win, false });

    EXPECT_FALSE(h.core.is_window_fullscreen(win));
}
