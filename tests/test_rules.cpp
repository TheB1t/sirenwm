#include <gtest/gtest.h>

#include <backend/commands.hpp>
#include <backend/events.hpp>
#include <config_types.hpp>
#include <core.hpp>

#include "test_harness.hpp"

// Pull in RulesModule without going through Lua config loader.
#include "../modules/rules/rules.hpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void apply_rules_event(TestHarness& h, WindowId win, bool from_restart = false) {
    h.emit(event::ApplyWindowRules{ win, from_restart });
}

static WindowId make_classified_window(TestHarness& h, WindowId id,
    const std::string& wm_class, const std::string& wm_instance = "") {
    h.core.dispatch(command::EnsureWindow{ id, 0 });
    command::SetWindowMetadata meta;
    meta.window      = id;
    meta.wm_class    = wm_class;
    meta.wm_instance = wm_instance.empty() ? wm_class : wm_instance;
    h.core.dispatch(meta);
    h.core.dispatch(command::SetWindowMapped{ id, true });
    return id;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(Rules, FloatRuleApplied) {
    TestHarness h;
    h.use<RulesModule>();

    WindowRule rule;
    rule.class_name   = "steam";
    rule.isfloating = true;
    h.config.add_window_rule(rule);

    h.start();

    WindowId win = make_classified_window(h, 0x1000, "steam");
    apply_rules_event(h, win);

    EXPECT_TRUE(h.core.window_state_any(win)->floating);
}

TEST(Rules, WorkspaceRuleRoutsWindow) {
    TestHarness h;
    h.use<RulesModule>();

    WindowRule rule;
    rule.class_name  = "firefox";
    rule.workspace = 1; // 0-indexed
    h.config.add_window_rule(rule);

    h.start();

    WindowId win = make_classified_window(h, 0x1000, "firefox");
    apply_rules_event(h, win);

    EXPECT_EQ(h.core.workspace_of_window(win), 1);
}

TEST(Rules, RulesSkippedOnRestart) {
    TestHarness h;
    h.use<RulesModule>();

    WindowRule rule;
    rule.class_name   = "steam";
    rule.isfloating = true;
    h.config.add_window_rule(rule);

    h.start();

    WindowId win = make_classified_window(h, 0x1000, "steam");
    // from_restart=true — rules must not override saved state
    apply_rules_event(h, win, /*from_restart=*/ true);

    // floating was not set by rule (window starts non-floating by default)
    EXPECT_FALSE(h.core.window_state_any(win)->floating);
}

TEST(Rules, CaseInsensitiveMatch) {
    TestHarness h;
    h.use<RulesModule>();

    WindowRule rule;
    rule.class_name   = "Firefox"; // mixed case in rule
    rule.isfloating = true;
    h.config.add_window_rule(rule);

    h.start();

    WindowId win = make_classified_window(h, 0x1000, "firefox"); // lowercase from X
    apply_rules_event(h, win);

    EXPECT_TRUE(h.core.window_state_any(win)->floating);
}

TEST(Rules, NoMatchLeavesWindowUntouched) {
    TestHarness h;
    h.use<RulesModule>();

    WindowRule rule;
    rule.class_name   = "steam";
    rule.isfloating = true;
    h.config.add_window_rule(rule);

    h.start();

    WindowId win = make_classified_window(h, 0x1000, "xterm");
    bool was_floating = h.core.window_state_any(win)->floating;
    apply_rules_event(h, win);

    EXPECT_EQ(h.core.window_state_any(win)->floating, was_floating);
}
