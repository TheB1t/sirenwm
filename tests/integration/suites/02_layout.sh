#!/usr/bin/env bash
# Layout tests: master/stack split, 3-window stack, geometry stability.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

switch_ws 0

# --- Two windows: master/stack ---
spawn_xclock "xclock_l1" "200x200"
sleep 0.3
spawn_xclock "xclock_l2" "200x200"
sleep 0.3

mapfile -t GEOMS < <(
    D xwininfo -root -tree 2>/dev/null \
        | grep -iE "xclock_l[12]" \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -2
)

if (( ${#GEOMS[@]} == 2 )); then
    G1="${GEOMS[0]}"; G2="${GEOMS[1]}"
    X1=$(geom_x "$G1"); W1=$(geom_w "$G1")
    X2=$(geom_x "$G2"); W2=$(geom_w "$G2")

    if (( X1 <= X2 )); then
        MX=$X1; MW=$W1; SX=$X2; SW=$W2
    else
        MX=$X2; MW=$W2; SX=$X1; SW=$W1
    fi

    MR=$(( MX + MW ))
    assert_lt "master right < stack left" "$MR" "$SX"
    assert_lt "stack narrower than master (factor 0.55)" "$SW" "$MW"
    pass "two windows: master/stack layout"
else
    skip "could not get both geometries for split test"
fi

# --- Geometry stable after workspace round-trip ---
mapfile -t BEFORE < <(
    D xwininfo -root -tree 2>/dev/null \
        | grep -iE "xclock_l[12]" \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -2
)

if (( ${#BEFORE[@]} == 2 )); then
    switch_ws 2; switch_ws 0; sleep 0.2

    mapfile -t AFTER < <(
        D xwininfo -root -tree 2>/dev/null \
            | grep -iE "xclock_l[12]" \
            | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -2
    )

    if (( ${#AFTER[@]} == 2 )); then
        geo_ok=1
        for idx in 0 1; do
            WB=$(geom_w "${BEFORE[$idx]}"); WA=$(geom_w "${AFTER[$idx]}")
            XB=$(geom_x "${BEFORE[$idx]}"); XA=$(geom_x "${AFTER[$idx]}")
            if (( WB != WA || XB != XA )); then
                fail "layout stable after round-trip (win $((idx+1)))" \
                    "before=${BEFORE[$idx]} after=${AFTER[$idx]}"
                geo_ok=0
            fi
        done
        (( geo_ok )) && pass "layout geometry stable after workspace round-trip"
    else
        skip "could not re-capture geometries"
    fi
fi

# --- Three windows: stack splits vertically ---
cleanup_spawned; sleep 0.3
spawn_xclock "xclock_a" "100x100"
spawn_xclock "xclock_b" "100x100"
spawn_xclock "xclock_c" "100x100"
sleep 0.5

mapfile -t G3 < <(
    D xwininfo -root -tree 2>/dev/null \
        | grep -iE "xclock_(a|b|c)" \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -3
)

if (( ${#G3[@]} == 3 )); then
    IFS=$'\n' sorted=($(for g in "${G3[@]}"; do echo "$(geom_x "$g") $g"; done | sort -n))

    S1="${sorted[1]#* }"; S2="${sorted[2]#* }"
    S1X=$(geom_x "$S1"); S2X=$(geom_x "$S2")
    S1Y=$(geom_y "$S1"); S1H=$(geom_h "$S1")
    S2Y=$(geom_y "$S2")

    assert_eq "3 windows: stack same X" "$S1X" "$S2X"

    S1_BOT=$(( S1Y + S1H ))
    if (( S2Y >= S1_BOT - 10 )); then
        pass "3 windows: stack arranged vertically"
    else
        fail "3 windows: stack vertical" "s1 bottom=$S1_BOT s2 y=$S2Y"
    fi
else
    skip "could not get 3 geometries (got ${#G3[@]})"
fi

cleanup_spawned
assert_alive "sirenwm alive after layout tests"
print_summary
(( FAIL == 0 ))
