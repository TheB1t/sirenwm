#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <domain/core.hpp>

#include "test_harness.hpp"

// ---------------------------------------------------------------------------
// SyncWindowFromConfigureNotify — client-initiated geometry changes
// ---------------------------------------------------------------------------

TEST(ConfigureNotify, UpdatesGeometryWhenNoPendingFlush) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);

    h.core.dispatch(command::atom::SyncWindowFromConfigureNotify{
        win, {100, 200}, {800, 600}, 2 });

    auto w = h.core.window_state_any(win);
    ASSERT_TRUE(w);
    EXPECT_EQ(w->pos().x(), 100);
    EXPECT_EQ(w->pos().y(), 200);
    EXPECT_EQ(w->size().x(), 800);
    EXPECT_EQ(w->size().y(), 600);
    EXPECT_EQ(w->border_width, 2);
}

TEST(ConfigureNotify, SkipsPositionWithPendingFlush) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);

    // Set position via WM — creates a pending flush
    h.core.dispatch(command::atom::SetWindowPosition{ win, {300, 400} });

    // Client sends ConfigureNotify with different position
    h.core.dispatch(command::atom::SyncWindowFromConfigureNotify{
        win, {999, 999}, {800, 600}, 1 });

    auto w = h.core.window_state_any(win);
    ASSERT_TRUE(w);
    // Position should NOT be overwritten (WM owns it)
    EXPECT_EQ(w->pos().x(), 300);
    EXPECT_EQ(w->pos().y(), 400);
    // Size should be updated (no pending size flush)
    EXPECT_EQ(w->size().x(), 800);
    EXPECT_EQ(w->size().y(), 600);
}

TEST(ConfigureNotify, SkipsSizeWithPendingGeometryFlush) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);

    // Set full geometry via WM
    h.core.dispatch(command::atom::SetWindowGeometry{ win, {100, 100}, {1024, 768} });

    // Client sends ConfigureNotify with different everything
    h.core.dispatch(command::atom::SyncWindowFromConfigureNotify{
        win, {0, 0}, {640, 480}, 0 });

    auto w = h.core.window_state_any(win);
    ASSERT_TRUE(w);
    // Nothing should be overwritten
    EXPECT_EQ(w->pos().x(), 100);
    EXPECT_EQ(w->pos().y(), 100);
    EXPECT_EQ(w->size().x(), 1024);
    EXPECT_EQ(w->size().y(), 768);
}

TEST(ConfigureNotify, SkipsBorderWidthWithPendingFlush) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);

    h.core.dispatch(command::atom::SetWindowBorderWidth{ win, 5 });

    h.core.dispatch(command::atom::SyncWindowFromConfigureNotify{
        win, {0, 0}, {800, 600}, 0 });

    auto w = h.core.window_state_any(win);
    ASSERT_TRUE(w);
    EXPECT_EQ(w->border_width, 5);
}

TEST(ConfigureNotify, AfterFlushConsumedAcceptsNew) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);

    h.core.dispatch(command::atom::SetWindowPosition{ win, {300, 400} });

    // Consume the pending flush (simulates backend applying it)
    h.core.take_window_flush(win);

    // Now ConfigureNotify should update position
    h.core.dispatch(command::atom::SyncWindowFromConfigureNotify{
        win, {50, 60}, {800, 600}, 1 });

    auto w = h.core.window_state_any(win);
    ASSERT_TRUE(w);
    EXPECT_EQ(w->pos().x(), 50);
    EXPECT_EQ(w->pos().y(), 60);
}

TEST(ConfigureNotify, NonexistentWindowReturnsFalse) {
    TestHarness h;
    h.start();

    bool ok = h.core.dispatch(command::atom::SyncWindowFromConfigureNotify{
        0xDEAD, {0, 0}, {800, 600}, 0 });
    EXPECT_FALSE(ok);
}

TEST(ConfigureNotify, UpdatesBorderWidthWhenNotPending) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);

    h.core.dispatch(command::atom::SyncWindowFromConfigureNotify{
        win, {0, 0}, {800, 600}, 7 });

    auto w = h.core.window_state_any(win);
    ASSERT_TRUE(w);
    EXPECT_EQ(w->border_width, 7);
}
