#!/usr/bin/env bash
# Floating tests: rule-based float, geometry preservation, mixed layout.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

switch_ws 0

# --- Rule-based floating: xclock_float preserves size ---
spawn_xclock "xclock_float" "200x200"
FW=$(wait_for_window "xclock_float") || true

if [[ -n "$FW" ]]; then
    pass "floating xclock spawned ($FW)"

    FG=$(D xwininfo -id "$FW" 2>/dev/null \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true)
    FWW=$(geom_w "$FG")

    if [[ -n "$FWW" ]] && (( FWW <= 300 )); then
        pass "floating window width preserved at client size ($FWW)"
    else
        fail "floating window size" "width=${FWW:-?}, expected <=300"
    fi
else
    fail "floating xclock spawned" "not found"
fi

# --- Floating window doesn't affect tiled layout ---
spawn_xclock "xclock_tiled1" "200x200"
T1=$(wait_for_window "xclock_tiled1") || true
spawn_xclock "xclock_tiled2" "200x200"
T2=$(wait_for_window "xclock_tiled2") || true
sleep 0.3

if [[ -n "$T1" && -n "$T2" ]]; then
    TW1=$(D xwininfo -id "$T1" 2>/dev/null | awk '/Width:/ {print $2; exit}' || true)
    TW2=$(D xwininfo -id "$T2" 2>/dev/null | awk '/Width:/ {print $2; exit}' || true)

    # Both tiled windows should be wider than floating (>300px)
    if [[ -n "$TW1" && -n "$TW2" ]] && (( TW1 > 300 && TW2 > 300 )); then
        pass "tiled windows not affected by floating ($TW1, $TW2)"
    else
        fail "tiled vs floating" "tiled widths: $TW1, $TW2"
    fi
fi

# --- Floating window survives workspace round-trip ---
if [[ -n "$FW" ]]; then
    FG_BEFORE=$(D xwininfo -id "$FW" 2>/dev/null \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true)

    switch_ws 2; switch_ws 0; sleep 0.2

    FG_AFTER=$(D xwininfo -id "$FW" 2>/dev/null \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true)

    if [[ "$FG_BEFORE" == "$FG_AFTER" ]]; then
        pass "floating geometry stable after workspace round-trip"
    else
        fail "floating round-trip" "before=$FG_BEFORE after=$FG_AFTER"
    fi
fi

# --- Multiple floating windows ---
spawn_xclock "xclock_float2" "150x150"
# xclock_float2 won't match the rule (instance != xclock_float), so
# it will be tiled. That's fine — we just test that adding windows works.
sleep 0.3
assert_alive "mixed tiled/floating doesn't crash"

cleanup_spawned
assert_alive "sirenwm alive after floating tests"
print_summary
(( FAIL == 0 ))
