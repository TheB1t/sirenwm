#!/usr/bin/env bash
# Focus tests: _NET_ACTIVE_WINDOW, _NET_WM_STATE_FOCUSED tracking, cycling.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

switch_ws 0

spawn_xclock "xclock_f1" "200x200"
W1=$(wait_for_window "xclock_f1") || true
spawn_xclock "xclock_f2" "200x200"
W2=$(wait_for_window "xclock_f2") || true
sleep 0.2

# Park pointer in bar area to avoid EnterNotify interference
if command -v xdotool >/dev/null 2>&1; then
    D xdotool mousemove 2 2
fi

# --- _NET_ACTIVE_WINDOW focuses visible client ---
if [[ -n "$W1" && -n "$W2" ]]; then
    focus_window "$W2"
    CUR=$(normalize_wid "$(active_window)")
    EXP=$(normalize_wid "$W2")
    assert_eq "_NET_ACTIVE_WINDOW focuses target" "$CUR" "$EXP"
fi

# --- _NET_WM_STATE_FOCUSED tracking ---
if [[ -n "$W1" && -n "$W2" ]]; then
    focus_window "$W1"
    sleep 0.2

    S1=$(window_prop "$W1" _NET_WM_STATE)
    S2=$(window_prop "$W2" _NET_WM_STATE)
    assert_contains "FOCUSED on focused window" "$S1" "FOCUSED"
    assert_not_contains "FOCUSED absent on unfocused" "$S2" "FOCUSED"
fi

# --- Focus cycling via EWMH ---
if [[ -n "$W1" && -n "$W2" ]]; then
    focus_window "$W1"
    if is_window_viewable "$W2"; then
        focus_window "$W2"
        CUR=$(normalize_wid "$(active_window)")
        EXP=$(normalize_wid "$W2")
        assert_eq "EWMH focus cycling works" "$CUR" "$EXP"
    else
        skip "W2 not viewable for cycling test"
    fi
fi

# --- FOCUSED cleared after workspace switch ---
if [[ -n "$W1" ]]; then
    focus_window "$W1"
    sleep 0.1
    switch_ws 2
    sleep 0.2
    S_AFTER=$(window_prop "$W1" _NET_WM_STATE)
    assert_not_contains "FOCUSED cleared after ws switch" "$S_AFTER" "FOCUSED"
    switch_ws 0
fi

# --- Focus tracks across 3 windows ---
spawn_xclock "xclock_f3" "200x200"
W3=$(wait_for_window "xclock_f3") || true
sleep 0.2

if [[ -n "$W1" && -n "$W2" && -n "$W3" ]]; then
    focus_window "$W1"
    focus_window "$W3"
    CUR=$(normalize_wid "$(active_window)")
    EXP=$(normalize_wid "$W3")
    assert_eq "focus tracks to 3rd window" "$CUR" "$EXP"

    S3=$(window_prop "$W3" _NET_WM_STATE)
    assert_contains "FOCUSED set on 3rd window" "$S3" "FOCUSED"
fi

cleanup_spawned
assert_alive "sirenwm alive after focus tests"
print_summary
(( FAIL == 0 ))
