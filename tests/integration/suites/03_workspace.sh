#!/usr/bin/env bash
# Workspace tests: switching, hiding/showing, rules, move between workspaces.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

# Spawn a window on ws 0
spawn_xclock "xclock_ws" "200x200"
WS_WIN=$(wait_for_window "xclock_ws") || true

# --- Starts on workspace 0 ---
assert_eq "starts on workspace 0" "$(current_desktop)" "0"

# --- Switch to workspace 1 ---
switch_ws 1
assert_eq "switched to workspace 1" "$(current_desktop)" "1"

# --- Previous windows hidden ---
if [[ -n "$WS_WIN" ]]; then
    assert_not_viewable "ws switch hides previous windows" "$WS_WIN"
fi

# --- Switch back ---
switch_ws 0
assert_eq "switched back to workspace 0" "$(current_desktop)" "0"

if [[ -n "$WS_WIN" ]]; then
    assert_viewable "returning ws remaps windows" "$WS_WIN"
fi

# --- Rule-routed window ---
if command -v xterm >/dev/null 2>&1; then
    spawn_xterm "wm-itest-xterm" "wm_itest_xterm"
    XTERM_ID=$(wait_for_window "wm-itest-xterm" 100) || true

    if [[ -n "$XTERM_ID" ]]; then
        pass "rule-routed xterm appears ($XTERM_ID)"
        assert_eq "rule does not steal workspace" "$(current_desktop)" "0"
        assert_not_viewable "rule-routed xterm hidden off-workspace" "$XTERM_ID"

        switch_ws 1
        for _ in $(seq 1 20); do
            is_window_viewable "$XTERM_ID" && break
            sleep 0.1
        done
        assert_viewable "xterm viewable on rule workspace" "$XTERM_ID"
        switch_ws 0
    else
        skip "could not find rule-routed xterm"
    fi
else
    skip "xterm not available for rule test"
fi

# --- Move window to workspace via wmctrl -t ---
if [[ -n "$WS_WIN" ]]; then
    D wmctrl -i -r "$WS_WIN" -t 1
    sleep 0.3

    DESK=$(window_prop "$WS_WIN" _NET_WM_DESKTOP | grep -oE '[0-9]+$' || true)
    assert_eq "wmctrl -t moves window" "$DESK" "1"
    assert_not_viewable "moved window hidden on inactive ws" "$WS_WIN"

    switch_ws 1
    assert_viewable "moved window visible on target ws" "$WS_WIN"

    # Move back
    D wmctrl -i -r "$WS_WIN" -t 0
    switch_ws 0; sleep 0.2
fi

cleanup_spawned
assert_alive "sirenwm alive after workspace tests"
print_summary
(( FAIL == 0 ))
