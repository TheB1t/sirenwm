#!/usr/bin/env bash
# Smoke tests: sirenwm starts, stays alive, basic window management works.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

# --- T1: sirenwm starts and stays alive ---
assert_alive "sirenwm starts and stays alive"

# --- T2: single window fills monitor ---
spawn_xclock "xclock_smoke" "200x200"
GEOM=""
for _ in $(seq 1 30); do
    sleep 0.1
    GEOM=$(get_geom "xclock_smoke")
    [[ -n "$GEOM" ]] && break
done

if [[ -z "$GEOM" ]]; then
    fail "xclock appears on screen" "xwininfo returned empty"
else
    pass "xclock appears on screen (geom: $GEOM)"
    W=$(geom_w "$GEOM"); H=$(geom_h "$GEOM")
    X=$(geom_x "$GEOM")
    assert_approx "xclock x near left edge" "$X" 4 10
    assert_lt "xclock width < screen width" "$W" $SCREEN_W
    assert_lt "xclock height < screen height" "$H" $SCREEN_H
fi

# --- T3: wallpaper mock receives correct args ---
sleep 0.5
if [[ ! -f "$WALLPAPER_ARGS_FILE" ]]; then
    fail "wallpaper: xwallpaper was not invoked" "args file not created"
else
    WP_ARGS=$(cat "$WALLPAPER_ARGS_FILE")
    if echo "$WP_ARGS" | grep -q "\-\-output.*\-\-stretch.*picture\.png"; then
        pass "wallpaper: xwallpaper invoked with correct args"
    else
        fail "wallpaper: xwallpaper args" "got: $WP_ARGS"
    fi
fi

# --- T4: still alive at end ---
assert_alive "sirenwm alive after smoke tests"

cleanup_spawned
print_summary
(( FAIL == 0 ))
