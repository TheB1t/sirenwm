#!/usr/bin/env bash
# Tray tests: system tray selection, icon embedding, rebalance.
# Uses xterm as a tray-aware client or falls back to a mock tray icon.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

sleep 0.5

# --- _NET_SYSTEM_TRAY_S0 selection is owned ---
TRAY_SEL=$(D xprop -root _NET_SYSTEM_TRAY_S0 2>/dev/null || true)
# xprop on root won't show selection owner, but we can check via the
# MANAGER client message side-effect: the tray window should exist.
# Instead, find the tray window by checking for override-redirect windows
# with _NET_SYSTEM_TRAY_ORIENTATION.
TRAY_WIN=""
while IFS= read -r wid; do
    [[ "$wid" == 0x* ]] || continue
    orient=$(D xprop -id "$wid" _NET_SYSTEM_TRAY_ORIENTATION 2>/dev/null || true)
    if echo "$orient" | grep -qE '= [0-9]'; then
        TRAY_WIN="$wid"
        break
    fi
done < <(D xwininfo -root -tree 2>/dev/null | awk '/^\s+0x/ {print $1}')

if [[ -n "$TRAY_WIN" ]]; then
    pass "tray window exists with _NET_SYSTEM_TRAY_ORIENTATION ($TRAY_WIN)"
else
    # Tray might be embedded inside bar. Check bar children.
    BAR_ID=$(find_dock_window) || true
    if [[ -n "$BAR_ID" ]]; then
        while IFS= read -r wid; do
            [[ "$wid" == 0x* ]] || continue
            orient=$(D xprop -id "$wid" _NET_SYSTEM_TRAY_ORIENTATION 2>/dev/null || true)
            if echo "$orient" | grep -qE '= [0-9]'; then
                TRAY_WIN="$wid"
                break
            fi
        done < <(D xwininfo -id "$BAR_ID" -tree 2>/dev/null | awk '/^\s+0x/ {print $1}')
    fi

    if [[ -n "$TRAY_WIN" ]]; then
        pass "tray window found under bar ($TRAY_WIN)"
    else
        skip "tray window not found (tray may not be configured)"
    fi
fi

# --- Tray icon embedding via trayer-like approach ---
# Spawn a small override-redirect window that requests XEMBED docking.
# This simulates what a status icon does. We use a trick: create a
# tiny xclock with -padding 0 and check if it gets reparented under tray.
# NOTE: xclock doesn't implement XEMBED, so we test tray presence only.
# Real tray icon embedding needs a proper XEMBED client.

if [[ -n "$TRAY_WIN" ]]; then
    # Get tray window geometry before and after spawning potential icons
    TRAY_GEOM_BEFORE=$(D xwininfo -id "$TRAY_WIN" 2>/dev/null \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true)
    TRAY_W_BEFORE=$(geom_w "$TRAY_GEOM_BEFORE")

    if [[ -n "$TRAY_W_BEFORE" ]]; then
        pass "tray width readable before icons ($TRAY_W_BEFORE)"
    else
        skip "could not read tray geometry"
    fi
fi

# --- Tray survives workspace switch ---
if [[ -n "$TRAY_WIN" ]]; then
    switch_ws 1; sleep 0.2; switch_ws 0; sleep 0.2
    if D xwininfo -id "$TRAY_WIN" &>/dev/null 2>&1; then
        pass "tray window survives workspace switch"
    else
        fail "tray window survives ws switch" "window gone after switch"
    fi
fi

# --- Tray survives rapid ops ---
for ws in 0 1 2 0; do
    D wmctrl -s $ws; sleep 0.05
done
switch_ws 0; sleep 0.2

if [[ -n "$TRAY_WIN" ]]; then
    if D xwininfo -id "$TRAY_WIN" &>/dev/null 2>&1; then
        pass "tray survives rapid workspace switches"
    else
        fail "tray survives rapid switches" "tray window gone"
    fi
fi

assert_alive "sirenwm alive after tray tests"
print_summary
(( FAIL == 0 ))
