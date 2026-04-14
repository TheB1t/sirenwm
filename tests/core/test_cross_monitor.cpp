#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <domain/core.hpp>

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

static std::unique_ptr<TestHarness> make_dual_monitor() {
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
// MoveWindowToMonitor
// ---------------------------------------------------------------------------

TEST(CrossMonitor, MoveWindowToMonitorBasic) {
    auto     h = make_dual_monitor();

    WindowId win = h->map_window(0x1000, 0);
    EXPECT_EQ(h->core.workspace_of_window(win), 0);

    bool ok = h->core.dispatch(command::atom::MoveWindowToMonitor{ win, 1 });
    EXPECT_TRUE(ok);
    EXPECT_EQ(h->core.workspace_of_window(win), 1);
}

TEST(CrossMonitor, MoveWindowToMonitorNoWindowUsesFocused) {
    auto     h = make_dual_monitor();

    WindowId win = h->map_window(0x1000, 0);
    h->core.dispatch(command::atom::FocusWindow{ win });

    bool ok = h->core.dispatch(command::atom::MoveWindowToMonitor{ NO_WINDOW, 1 });
    EXPECT_TRUE(ok);
    EXPECT_EQ(h->core.workspace_of_window(win), 1);
}

TEST(CrossMonitor, MoveWindowToMonitorInvalidMonitor) {
    auto     h   = make_dual_monitor();
    WindowId win = h->map_window(0x1000, 0);

    bool     ok = h->core.dispatch(command::atom::MoveWindowToMonitor{ win, 99 });
    EXPECT_FALSE(ok);
    EXPECT_EQ(h->core.workspace_of_window(win), 0);
}

// ---------------------------------------------------------------------------
// Fullscreen cross-monitor stacking
// ---------------------------------------------------------------------------

TEST(CrossMonitor, FullscreenMoveUpdatesSourceWorkspaceMode) {
    auto     h = make_dual_monitor();

    WindowId win = h->map_window(0x1000, 0);
    h->core.dispatch(command::atom::SetWindowFullscreen{ win, true });
    EXPECT_EQ(h->core.workspace_states()[0].mode, WorkspaceMode::Fullscreen);

    h->core.dispatch(command::atom::MoveWindowToWorkspace{ win, 1 });

    // Source workspace should exit fullscreen mode
    EXPECT_EQ(h->core.workspace_states()[0].mode, WorkspaceMode::Normal);
    // Destination workspace should enter fullscreen mode
    EXPECT_EQ(h->core.workspace_states()[1].mode, WorkspaceMode::Fullscreen);
}

TEST(CrossMonitor, BorderlessMoveRepinsGeometry) {
    auto     h = make_dual_monitor();

    WindowId win = h->map_window(0x1000, 0);
    h->core.dispatch(command::atom::SetWindowBorderless{ win, true });

    // Should be pinned to primary (1920x1080)
    auto w = h->core.window_state_any(win);
    EXPECT_EQ(w->size().x(), 1920);
    EXPECT_EQ(w->size().y(), 1080);

    h->core.dispatch(command::atom::MoveWindowToWorkspace{ win, 1 });

    // Should be re-pinned to secondary (2560x1440)
    w = h->core.window_state_any(win);
    EXPECT_EQ(w->pos().x(), 1920);
    EXPECT_EQ(w->size().x(), 2560);
    EXPECT_EQ(w->size().y(), 1440);
}

TEST(CrossMonitor, FullscreenMoveEmitsAssignedEvent) {
    auto     h = make_dual_monitor();

    WindowId win = h->map_window(0x1000, 0);
    h->core.dispatch(command::atom::SetWindowFullscreen{ win, true });
    h->take_core_events(); // drain

    h->core.dispatch(command::atom::MoveWindowToWorkspace{ win, 1 });
    auto events = h->take_core_events();

    bool found = false;
    for (const auto& e : events) {
        if (auto* ev = std::get_if<event::WindowAssignedToWorkspace>(&e)) {
            if (ev->window == win && ev->workspace_id == 1) {
                found = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found);
}

TEST(CrossMonitor, NormalWindowMoveDoesNotTriggerFullscreenEval) {
    auto     h = make_dual_monitor();

    WindowId win = h->map_window(0x1000, 0);
    drain(h->core);

    h->core.dispatch(command::atom::MoveWindowToWorkspace{ win, 1 });

    // Both workspaces should stay Normal
    EXPECT_EQ(h->core.workspace_states()[0].mode, WorkspaceMode::Normal);
    EXPECT_EQ(h->core.workspace_states()[1].mode, WorkspaceMode::Normal);
}

// ---------------------------------------------------------------------------
// Focus follows cross-monitor move
// ---------------------------------------------------------------------------

TEST(CrossMonitor, FocusFollowsMovedWindowToVisibleWorkspace) {
    auto     h = make_dual_monitor();

    WindowId win = h->map_window(0x1000, 0);
    drain(h->core);

    h->core.dispatch(command::atom::MoveWindowToWorkspace{ win, 1 });
    auto effects = drain(h->core);

    // Window moved to visible workspace 1 — should get focus
    EXPECT_TRUE(has_effect(effects, BackendEffectKind::FocusWindow, win));
}

TEST(CrossMonitor, FocusDoesNotFollowMoveToInvisibleWorkspace) {
    auto     h = make_dual_monitor();

    WindowId win = h->map_window(0x1000, 0);
    drain(h->core);

    // workspace 2 is not visible on any monitor
    h->core.dispatch(command::atom::MoveWindowToWorkspace{ win, 2 });
    auto effects = drain(h->core);

    EXPECT_FALSE(has_effect(effects, BackendEffectKind::FocusWindow, win));
}

// ---------------------------------------------------------------------------
// Cross-monitor move of a borderless window emits FocusChanged,
// which is what the backend uses to re-sync pointer barriers.
// ---------------------------------------------------------------------------

TEST(CrossMonitor, MoveBorderlessAcrossMonitorsEmitsFocusChanged) {
    auto     h = make_dual_monitor();

    WindowId win = h->map_window(0x1000, 0);
    h->core.dispatch(command::atom::SetWindowBorderless{ win, true });
    h->take_core_events(); // drain

    h->core.dispatch(command::atom::MoveWindowToWorkspace{ win, 1 });
    auto events = h->take_core_events();

    EXPECT_TRUE(has_domain_event<event::FocusChanged>(events));
}

// ---------------------------------------------------------------------------
// Three monitors
// ---------------------------------------------------------------------------

TEST(CrossMonitor, MoveAcrossThreeMonitors) {
    TestHarness h({
        make_monitor(0, 0,    0, 1920, 1080, "left"),
        make_monitor(1, 1920, 0, 2560, 1440, "center"),
        make_monitor(2, 4480, 0, 1280,  720, "right"),
    });
    CoreSettings s;
    s.workspace_defs = { { "[1]", "left" }, { "[2]", "center" }, { "[3]", "right" } };
    h.core.apply_settings(s);
    h.start();

    WindowId win = h.map_window(0x1000, 0);
    h.core.dispatch(command::atom::SetWindowFullscreen{ win, true });

    // Move left -> center
    h.core.dispatch(command::atom::MoveWindowToWorkspace{ win, 1 });
    auto w = h.core.window_state_any(win);
    EXPECT_EQ(w->pos().x(), 1920);
    EXPECT_EQ(w->size().x(), 2560);

    // Move center -> right
    h.core.dispatch(command::atom::MoveWindowToWorkspace{ win, 2 });
    w = h.core.window_state_any(win);
    EXPECT_EQ(w->pos().x(), 4480);
    EXPECT_EQ(w->size().x(), 1280);
    EXPECT_EQ(w->size().y(), 720);
}
