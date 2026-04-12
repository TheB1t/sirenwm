#!/usr/bin/env bash
# Fullscreen tests: add/remove, covers bar, geometry, stacking.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

switch_ws 0

spawn_xclock "xclock_fs1" "200x200"
FS1=$(wait_for_window "xclock_fs1") || true
sleep 0.2

if [[ -z "$FS1" ]]; then
    fail "fullscreen test window" "not found"
    print_summary
    exit 1
fi

# --- Add fullscreen ---
D wmctrl -i -r "$FS1" -b add,fullscreen
sleep 0.4

FS_GEOM=$(D xwininfo -id "$FS1" 2>/dev/null \
    | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true)
FS_X=$(geom_x "$FS_GEOM"); FS_Y=$(geom_y "$FS_GEOM")
FS_W=$(geom_w "$FS_GEOM"); FS_H=$(geom_h "$FS_GEOM")

assert_approx "fullscreen x = 0" "${FS_X:-999}" 0 4
assert_approx "fullscreen y = 0 (covers bar)" "${FS_Y:-999}" 0 4
assert_approx "fullscreen width = screen" "${FS_W:-0}" $SCREEN_W 4
assert_approx "fullscreen height = screen" "${FS_H:-0}" $SCREEN_H 4

# --- _NET_WM_STATE_FULLSCREEN property ---
FS_PROP=$(window_prop "$FS1" _NET_WM_STATE)
assert_contains "_NET_WM_STATE_FULLSCREEN set" "$FS_PROP" "FULLSCREEN"

# --- Remove fullscreen ---
D wmctrl -i -r "$FS1" -b remove,fullscreen
sleep 0.8

NOFS_W=$(D xwininfo -id "$FS1" 2>/dev/null | awk '/Width:/ {print $2; exit}' || true)
NOFS_H=$(D xwininfo -id "$FS1" 2>/dev/null | awk '/Height:/ {print $2; exit}' || true)

if [[ -n "$NOFS_W" ]] && (( NOFS_W > 0 && NOFS_W < SCREEN_W )); then
    pass "fullscreen remove restores tiled width (w=$NOFS_W)"
else
    fail "fullscreen restore width" "w=${NOFS_W:-?}"
fi

if [[ -n "$NOFS_H" ]] && (( NOFS_H > 0 && NOFS_H < SCREEN_H )); then
    pass "fullscreen remove restores tiled height (h=$NOFS_H)"
else
    fail "fullscreen restore height" "h=${NOFS_H:-?}"
fi

NOFS_PROP=$(window_prop "$FS1" _NET_WM_STATE)
assert_not_contains "_NET_WM_STATE_FULLSCREEN cleared" "$NOFS_PROP" "FULLSCREEN"

# --- Second fullscreen window (different test) ---
spawn_xclock "xclock_fs2" "100x100"
FS2=$(wait_for_window "xclock_fs2") || true
if [[ -n "$FS2" ]]; then
    D wmctrl -i -r "$FS2" -b add,fullscreen
    sleep 0.4

    FS2_Y=$(D xwininfo -id "$FS2" 2>/dev/null \
        | awk '/Absolute upper-left Y:/ {print $4}' || true)
    FS2_H=$(D xwininfo -id "$FS2" 2>/dev/null \
        | awk '/Height:/ {print $2}' || true)
    FS2_W=$(D xwininfo -id "$FS2" 2>/dev/null \
        | awk '/Width:/ {print $2}' || true)

    assert_approx "fs2 covers full height" "${FS2_H:-0}" $SCREEN_H 4
    assert_approx "fs2 covers full width" "${FS2_W:-0}" $SCREEN_W 4
    assert_approx "fs2 y = 0 (over bar)" "${FS2_Y:-99}" 0 4

    D wmctrl -i -r "$FS2" -b remove,fullscreen
    sleep 0.5

    FS2_H_AFTER=$(D xwininfo -id "$FS2" 2>/dev/null | awk '/Height:/ {print $2}' || true)
    if [[ -n "$FS2_H_AFTER" ]] && (( FS2_H_AFTER < SCREEN_H )); then
        pass "fs2 restored to tiled height (h=$FS2_H_AFTER)"
    else
        fail "fs2 restore" "h=${FS2_H_AFTER:-?}"
    fi
fi

# --- Fullscreen toggle rapid ---
if [[ -n "$FS1" ]]; then
    for _ in 1 2 3; do
        D wmctrl -i -r "$FS1" -b add,fullscreen
        sleep 0.15
        D wmctrl -i -r "$FS1" -b remove,fullscreen
        sleep 0.15
    done
    assert_alive "rapid fullscreen toggle doesn't crash"
fi

cleanup_spawned
assert_alive "sirenwm alive after fullscreen tests"
print_summary
(( FAIL == 0 ))
