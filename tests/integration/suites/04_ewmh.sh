#!/usr/bin/env bash
# EWMH compliance tests: root properties, per-window atoms, protocol atoms.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

switch_ws 0
spawn_xclock "xclock_ewmh1" "100x100"
W1=$(wait_for_window "xclock_ewmh1") || true
spawn_xclock "xclock_ewmh2" "100x100"
W2=$(wait_for_window "xclock_ewmh2") || true
sleep 0.2

# --- _NET_CLIENT_LIST ---
CL=$(root_prop _NET_CLIENT_LIST)
assert_contains "_NET_CLIENT_LIST is set" "$CL" "window id"

# --- _NET_CLIENT_LIST_STACKING ---
ST=$(root_prop _NET_CLIENT_LIST_STACKING)
assert_contains "_NET_CLIENT_LIST_STACKING is set" "$ST" "window id"

# --- _NET_NUMBER_OF_DESKTOPS ---
NDKS=$(root_prop _NET_NUMBER_OF_DESKTOPS | grep -oE '[0-9]+$' || true)
assert_eq "_NET_NUMBER_OF_DESKTOPS = 3" "$NDKS" "3"

# --- _NET_DESKTOP_NAMES ---
DNAMES=$(root_prop _NET_DESKTOP_NAMES)
for dname in "[1]" "[2]" "[3]"; do
    assert_contains "_NET_DESKTOP_NAMES has $dname" "$DNAMES" "$dname"
done

# --- _NET_WORKAREA ---
WA=$(root_prop _NET_WORKAREA)
if echo "$WA" | grep -qE '[0-9]{3,}'; then
    pass "_NET_WORKAREA has non-zero dimensions"
else
    fail "_NET_WORKAREA" "got: $WA"
fi

# --- _NET_DESKTOP_VIEWPORT ---
VP=$(root_prop _NET_DESKTOP_VIEWPORT)
assert_contains "_NET_DESKTOP_VIEWPORT is set" "$VP" "0"

# --- _NET_SUPPORTING_WM_CHECK ---
WMC=$(root_prop _NET_SUPPORTING_WM_CHECK)
if echo "$WMC" | grep -q "window id"; then
    WMC_ID=$(echo "$WMC" | grep -oE '0x[0-9a-fA-F]+' | head -1 || true)
    if [[ -n "$WMC_ID" ]]; then
        WM_NAME=$(xprop_win "$WMC_ID" _NET_WM_NAME || true)
        if echo "$WM_NAME" | grep -qi "siren\|swm"; then
            pass "_NET_SUPPORTING_WM_CHECK with WM name"
        else
            pass "_NET_SUPPORTING_WM_CHECK is set ($WMC_ID)"
        fi
    else
        fail "_NET_SUPPORTING_WM_CHECK" "no window ID"
    fi
else
    fail "_NET_SUPPORTING_WM_CHECK" "not found"
fi

# --- Per-window: _NET_WM_DESKTOP ---
if [[ -n "$W1" ]]; then
    D1=$(window_prop "$W1" _NET_WM_DESKTOP | grep -oE '[0-9]+$' || true)
    assert_eq "_NET_WM_DESKTOP = 0 for ws-0 window" "$D1" "0"
fi

# --- _NET_FRAME_EXTENTS ---
if [[ -n "$W1" ]]; then
    EXT=$(window_prop "$W1" _NET_FRAME_EXTENTS | grep -oE '[0-9, ]+$' || true)
    if echo "$EXT" | grep -qE '^[[:space:]]*0,[[:space:]]*0,[[:space:]]*0,[[:space:]]*0'; then
        pass "_NET_FRAME_EXTENTS = 0,0,0,0"
    else
        fail "_NET_FRAME_EXTENTS" "got: $EXT"
    fi
fi

# --- _NET_WM_PID ---
if [[ -n "$W1" ]]; then
    WPID=$(window_prop "$W1" _NET_WM_PID | grep -oE '[0-9]+$' || true)
    if [[ -n "$WPID" ]] && (( WPID > 0 )); then
        pass "_NET_WM_PID is set (pid=$WPID)"
    else
        skip "_NET_WM_PID not set by xclock (client responsibility)"
    fi
fi

# --- _NET_SUPPORTED contains key atoms ---
SUPPORTED=$(root_prop _NET_SUPPORTED)
for atom in _NET_ACTIVE_WINDOW _NET_CLIENT_LIST _NET_CURRENT_DESKTOP \
            _NET_WM_STATE _NET_WM_STATE_FULLSCREEN _NET_CLOSE_WINDOW \
            _NET_WM_DESKTOP _NET_WM_NAME; do
    assert_contains "_NET_SUPPORTED has $atom" "$SUPPORTED" "$atom"
done

cleanup_spawned
assert_alive "sirenwm alive after EWMH tests"
print_summary
(( FAIL == 0 ))
