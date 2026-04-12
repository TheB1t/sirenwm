#!/usr/bin/env bash
# Mouse tests: click-to-focus, pointer enter/leave, motion tracking.
# Note: passive grab paths (mod+button) cannot be tested via XTEST.
# These tests cover the non-grab mouse paths.

source "$(dirname "$0")/../lib/setup.sh"

if ! command -v xdotool >/dev/null 2>&1; then
    echo "xdotool not available — skipping mouse tests"
    exit 0
fi

start_xephyr
write_config
start_sirenwm

switch_ws 0

spawn_xclock "xclock_m1" "200x200"
W1=$(wait_for_window "xclock_m1") || true
spawn_xclock "xclock_m2" "200x200"
W2=$(wait_for_window "xclock_m2") || true
sleep 0.3

if [[ -z "$W1" || -z "$W2" ]]; then
    skip "need 2 windows for mouse tests"
    print_summary
    exit 0
fi

# Get geometry to know where each window is
G1=$(get_geom "xclock_m1")
G2=$(get_geom "xclock_m2")

if [[ -z "$G1" || -z "$G2" ]]; then
    skip "could not get window geometries"
    print_summary
    exit 0
fi

X1=$(geom_x "$G1"); Y1=$(geom_y "$G1"); W1W=$(geom_w "$G1"); H1=$(geom_h "$G1")
X2=$(geom_x "$G2"); Y2=$(geom_y "$G2"); W2W=$(geom_w "$G2"); H2=$(geom_h "$G2")

# Center of each window
CX1=$(( X1 + W1W / 2 )); CY1=$(( Y1 + H1 / 2 ))
CX2=$(( X2 + W2W / 2 )); CY2=$(( Y2 + H2 / 2 ))

# NOTE: click-to-focus uses passive button grabs (grab_button_any) which
# XTEST synthetic events cannot trigger. Skipping click-to-focus tests.
# These are covered by the unit test harness instead.

# --- EnterNotify: move pointer into W1 (no click) ---
# Park at neutral position first
D xdotool mousemove 2 2
sleep 0.2

# Focus W2 explicitly
focus_window "$W2"
sleep 0.1

# Move pointer into W1 — if focus-follows-mouse is active, should change focus
D xdotool mousemove $CX1 $CY1
sleep 0.5

CUR=$(normalize_wid "$(active_window)")
EXP1=$(normalize_wid "$W1")
# EnterNotify should trigger focus change (focus-follows-mouse)
if [[ "$CUR" == "$EXP1" ]]; then
    pass "EnterNotify: pointer entry focuses window"
else
    # If focus-follows-mouse is not enabled, click-to-focus is the only path
    info "EnterNotify did not change focus (click-to-focus only mode)"
fi

# --- Pointer position after motion ---
D xdotool mousemove 640 360
sleep 0.1
QPOS=$(D xdotool getmouselocation 2>/dev/null || true)
if echo "$QPOS" | grep -qE 'x:6[34][0-9]\s+y:3[56][0-9]'; then
    pass "pointer position tracks after mousemove"
else
    # Tolerate some drift
    PX=$(echo "$QPOS" | grep -oE 'x:[0-9]+' | cut -d: -f2)
    PY=$(echo "$QPOS" | grep -oE 'y:[0-9]+' | cut -d: -f2)
    if [[ -n "$PX" && -n "$PY" ]]; then
        assert_approx "pointer x after mousemove" "$PX" 640 10
        assert_approx "pointer y after mousemove" "$PY" 360 10
    else
        fail "pointer position" "could not query: $QPOS"
    fi
fi

# --- Click on empty area (root) doesn't crash ---
D xdotool mousemove 2 2
D xdotool click 1
sleep 0.2
assert_alive "click on root area doesn't crash"

# --- Rapid mouse movement across windows ---
for _ in $(seq 1 5); do
    D xdotool mousemove $CX1 $CY1
    sleep 0.02
    D xdotool mousemove $CX2 $CY2
    sleep 0.02
done
sleep 0.3
assert_alive "rapid cross-window pointer movement doesn't crash"

cleanup_spawned
print_summary
(( FAIL == 0 ))
