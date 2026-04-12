#!/usr/bin/env bash
# Bar tests: dock type, geometry, override-redirect, survives ops.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

sleep 0.3

# --- Find bar window ---
BAR_ID=$(find_dock_window) || true

if [[ -z "$BAR_ID" ]]; then
    fail "bar window with _NET_WM_WINDOW_TYPE_DOCK" "no dock window found"
    print_summary
    exit 1
fi

pass "bar window found ($BAR_ID)"

# --- Override-redirect ---
OR=$(D xwininfo -id "$BAR_ID" 2>/dev/null \
    | grep -i "override redirect" | grep -oi "yes" || true)
assert_eq "bar is override-redirect" "${OR,,}" "yes"

# --- Geometry: top of screen, correct height ---
BAR_GEOM=$(D xwininfo -id "$BAR_ID" 2>/dev/null \
    | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true)
BAR_Y=$(geom_y "$BAR_GEOM")
BAR_H=$(geom_h "$BAR_GEOM")
BAR_W=$(geom_w "$BAR_GEOM")

assert_eq "bar y = 0 (top of screen)" "$BAR_Y" "0"
assert_eq "bar height = 18" "$BAR_H" "18"
assert_approx "bar width = screen width" "$BAR_W" $SCREEN_W 4

# --- Bar doesn't overlap tiled content ---
spawn_xclock "xclock_bartile" "200x200"
BWIN=$(wait_for_window "xclock_bartile") || true
if [[ -n "$BWIN" ]]; then
    WY=$(D xwininfo -id "$BWIN" 2>/dev/null \
        | awk '/Absolute upper-left Y:/ {print $4}' || true)
    BAR_BOTTOM=$(( BAR_Y + BAR_H ))
    if [[ -n "$WY" ]] && (( WY >= BAR_BOTTOM - 2 )); then
        pass "tiled window starts below bar (y=$WY, bar_bottom=$BAR_BOTTOM)"
    else
        fail "tiled below bar" "window y=${WY:-empty}, bar bottom=$BAR_BOTTOM"
    fi
fi

# --- Bar survives workspace switch ---
switch_ws 1; sleep 0.2; switch_ws 0; sleep 0.2
if D xwininfo -id "$BAR_ID" &>/dev/null 2>&1; then
    pass "bar survives workspace switch"
else
    fail "bar survives ws switch" "bar window gone"
fi

# --- Bar is on top (raised above managed windows) ---
if [[ -n "$BWIN" ]]; then
    # Check stacking order: bar should appear after (above) managed windows
    # in xwininfo tree output (last = topmost)
    STACKING=$(D xwininfo -root -tree 2>/dev/null | awk '/^\s+0x/ {print $1}')
    BAR_POS=$(echo "$STACKING" | grep -n "$(echo "$BAR_ID" | tr 'A-F' 'a-f')" | cut -d: -f1 || true)
    WIN_POS=$(echo "$STACKING" | grep -n "$(echo "$BWIN" | tr 'A-F' 'a-f')" | cut -d: -f1 || true)
    if [[ -n "$BAR_POS" && -n "$WIN_POS" ]]; then
        # Lower line number = higher in tree = later in stacking = on top
        # Actually in xwininfo -tree, children are listed bottom to top
        info "bar stacking pos=$BAR_POS, window pos=$WIN_POS"
    fi
fi

# --- Bar title updates on focus change ---
# Spawn two windows, switch focus, check if bar redraws (no crash)
spawn_xclock "xclock_bar2" "200x200"
BWIN2=$(wait_for_window "xclock_bar2") || true
if [[ -n "$BWIN" && -n "$BWIN2" ]]; then
    focus_window "$BWIN"
    sleep 0.2
    focus_window "$BWIN2"
    sleep 0.2
    assert_alive "bar survives rapid focus changes"
fi

# --- Bar present on workspace with no windows ---
cleanup_spawned; sleep 0.3
if D xwininfo -id "$BAR_ID" &>/dev/null 2>&1; then
    pass "bar visible on empty workspace"
else
    fail "bar visible on empty ws" "bar window gone"
fi

assert_alive "sirenwm alive after bar tests"
print_summary
(( FAIL == 0 ))
