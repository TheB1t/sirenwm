#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <core.hpp>

#include "test_harness.hpp"

// ---------------------------------------------------------------------------
// Workspace switch
// ---------------------------------------------------------------------------

TEST(Workspace, SwitchChangesActiveWorkspace) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::SwitchWorkspace{ 1, std::nullopt });
    EXPECT_EQ(h.core.active_workspace_on_monitor(0), 1);
}

TEST(Workspace, MoveWindowToWorkspace) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::MoveWindowToWorkspace{ win, 1 });
    EXPECT_EQ(h.core.workspace_of_window(win), 1);
}

TEST(Workspace, WindowHiddenAfterMoveToInactiveWorkspace) {
    TestHarness h;
    h.start();

    // ws 0 is active; move window to ws 1 which is not active
    h.core.dispatch(command::SwitchWorkspace{ 0, std::nullopt });
    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::MoveWindowToWorkspace{ win, 1 });

    auto ws = h.core.window_state_any(win);
    ASSERT_NE(ws, nullptr);
    EXPECT_FALSE(ws->is_visible());
}

TEST(Workspace, FocusWindowUpdatesState) {
    TestHarness h;
    h.start();

    WindowId a = h.map_window(0x1001, 0);
    WindowId b = h.map_window(0x1002, 0);

    h.core.dispatch(command::FocusWindow{ a });
    EXPECT_EQ(h.core.focus_state().window, a);

    h.core.dispatch(command::FocusWindow{ b });
    EXPECT_EQ(h.core.focus_state().window, b);
}

TEST(Workspace, SwitchBackRestoresWindows) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::SwitchWorkspace{ 1, std::nullopt });
    EXPECT_FALSE(h.core.window_state_any(win)->is_visible());

    h.core.dispatch(command::SwitchWorkspace{ 0, std::nullopt });
    EXPECT_TRUE(h.core.window_state_any(win)->is_visible());
}

// ---------------------------------------------------------------------------
// Multi-monitor
// ---------------------------------------------------------------------------

TEST(Monitor, FocusMonitorUpdatesFocusedIndex) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    h.core.dispatch(command::FocusMonitor{ 1 });
    EXPECT_EQ(h.core.focused_monitor_index(), 1);
}

TEST(Monitor, EachMonitorGetsAtLeastOneWorkspace) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 1920, 1080, "secondary"),
    });
    h.start();

    // With 2 monitors and 3 workspaces, each monitor should own at least one
    EXPECT_GE(h.core.workspace_count(), 2);
    // Workspaces are distributed across monitors — each monitor has an active ws
    EXPECT_GE(h.core.active_workspace_on_monitor(0), 0);
    EXPECT_GE(h.core.active_workspace_on_monitor(1), 0);
}

// ---------------------------------------------------------------------------
// Floating
// ---------------------------------------------------------------------------

TEST(Window, ToggleFloating) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    bool     was = h.core.window_state_any(win)->floating;

    h.core.dispatch(command::ToggleWindowFloating{ win });
    EXPECT_NE(h.core.window_state_any(win)->floating, was);

    h.core.dispatch(command::ToggleWindowFloating{ win });
    EXPECT_EQ(h.core.window_state_any(win)->floating, was);
}

// ---------------------------------------------------------------------------
// Fullscreen/borderless windows can move across monitors and get re-pinned
// ---------------------------------------------------------------------------

TEST(Window, FullscreenCrossMonitorMove) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 2560, 1440, "secondary"),
    });
    CoreSettings s;
    s.workspace_defs = { { "[1]", "primary" }, { "[2]", "secondary" } };
    h.core.apply_settings(s);
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::SetWindowFullscreen{ win, true });

    bool moved = h.core.dispatch(command::MoveWindowToWorkspace{ win, 1 });
    EXPECT_TRUE(moved);
    EXPECT_EQ(h.core.workspace_of_window(win), 1);

    auto ws = h.core.window_state_any(win);
    ASSERT_TRUE(ws);
    EXPECT_EQ(ws->pos().x(), 1920);
    EXPECT_EQ(ws->size().x(), 2560);
    EXPECT_EQ(ws->size().y(), 1440);
}

TEST(Window, BorderlessCrossMonitorMove) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "primary"),
        make_monitor(1, 1920, 0, 2560, 1440, "secondary"),
    });
    CoreSettings s;
    s.workspace_defs = { { "[1]", "primary" }, { "[2]", "secondary" } };
    h.core.apply_settings(s);
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::SetWindowBorderless{ win, true });

    bool moved = h.core.dispatch(command::MoveWindowToWorkspace{ win, 1 });
    EXPECT_TRUE(moved);
    EXPECT_EQ(h.core.workspace_of_window(win), 1);

    auto ws = h.core.window_state_any(win);
    ASSERT_TRUE(ws);
    EXPECT_EQ(ws->pos().x(), 1920);
    EXPECT_EQ(ws->size().x(), 2560);
    EXPECT_EQ(ws->size().y(), 1440);
}

// ---------------------------------------------------------------------------
// Domain events
// ---------------------------------------------------------------------------

TEST(Workspace, SwitchEmitsWorkspaceSwitchedEvent) {
    TestHarness h;
    h.start();
    h.core.take_core_events(); // drain

    h.core.dispatch(command::SwitchWorkspace{ 1, std::nullopt });
    auto events = h.core.take_core_events();

    bool found = false;
    for (const auto& e : events) {
        if (auto* ev = std::get_if<event::WorkspaceSwitched>(&e)) {
            if (ev->workspace_id == 1) {
                found = true; break;
            }
        }
    }
    EXPECT_TRUE(found);
}

TEST(Workspace, MoveWindowEmitsAssignedEvent) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.take_core_events(); // drain

    h.core.dispatch(command::MoveWindowToWorkspace{ win, 1 });
    auto events = h.core.take_core_events();

    bool found = false;
    for (const auto& e : events) {
        if (auto* ev = std::get_if<event::WindowAssignedToWorkspace>(&e)) {
            if (ev->window == win && ev->workspace_id == 1) {
                found = true; break;
            }
        }
    }
    EXPECT_TRUE(found);
}

TEST(Workspace, BorderlessEmitsBorderlessActivatedEvent) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.take_core_events(); // drain

    h.core.dispatch(command::SetWindowBorderless{ win, true });
    auto events = h.core.take_core_events();

    bool found = false;
    for (const auto& e : events) {
        if (auto* ev = std::get_if<event::BorderlessActivated>(&e)) {
            if (ev->window == win) {
                found = true; break;
            }
        }
    }
    EXPECT_TRUE(found);
}

TEST(Workspace, ClearLastBorderlessEmitsDeactivatedEvent) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::SetWindowBorderless{ win, true });
    h.core.take_core_events(); // drain

    h.core.dispatch(command::SetWindowBorderless{ win, false });
    auto events = h.core.take_core_events();

    bool found = false;
    for (const auto& e : events) {
        if (std::holds_alternative<event::BorderlessDeactivated>(e)) {
            found = true; break;
        }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Zoom
// ---------------------------------------------------------------------------

TEST(Workspace, ZoomSwapsFocusedWithMaster) {
    TestHarness h;
    h.start();

    h.map_window(0x1000, 0);
    h.map_window(0x2000, 0);
    WindowId w3 = h.map_window(0x3000, 0);

    h.core.dispatch(command::FocusWindow{ w3 });

    bool ok = h.core.dispatch(command::Zoom{});
    EXPECT_TRUE(ok);

    auto& wst = h.core.workspace_states()[0];
    ASSERT_FALSE(wst.windows.empty());
    EXPECT_EQ(wst.windows[0]->id, w3);
}

TEST(Workspace, ZoomOnSingleWindowReturnsFalse) {
    TestHarness h;
    h.start();

    h.map_window(0x1000, 0);
    bool ok = h.core.dispatch(command::Zoom{});
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// IncMaster / AdjustMasterFactor
// ---------------------------------------------------------------------------

TEST(Workspace, IncMasterChangesCount) {
    TestHarness h;
    h.start();

    h.core.dispatch(command::IncMaster{ 1 });
    EXPECT_EQ(h.core.cfg().nmaster, 2);

    h.core.dispatch(command::IncMaster{ -1 });
    EXPECT_EQ(h.core.cfg().nmaster, 1);
}

TEST(Workspace, AdjustMasterFactorChangesRatio) {
    TestHarness h;
    h.start();

    double before = h.core.cfg().master_factor;
    h.core.dispatch(command::AdjustMasterFactor{ 0.05 });
    double after = h.core.cfg().master_factor;
    EXPECT_GT(after, before);
}

// ---------------------------------------------------------------------------
// HideWindow
// ---------------------------------------------------------------------------

TEST(Workspace, HideWindowMakesInvisible) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    EXPECT_TRUE(h.core.window_state_any(win)->is_visible());

    h.core.dispatch(command::HideWindow{ win });
    EXPECT_FALSE(h.core.window_state_any(win)->is_visible());
}

TEST(Workspace, HideWindowEmitsUnmap) {
    TestHarness h;
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.take_backend_effects(); // drain

    h.core.dispatch(command::HideWindow{ win });
    auto effects = h.core.take_backend_effects();

    bool found = false;
    for (const auto& e : effects) {
        if (e.kind == BackendEffectKind::UnmapWindow && e.window == win) {
            found = true; break;
        }
    }
    EXPECT_TRUE(found);
}
