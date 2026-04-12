#!/usr/bin/env bash
# Window lifecycle: map, unmap, destroy, configure request, rapid churn.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

switch_ws 0

# --- Map: new window appears in client list and is viewable ---
COUNT_BEFORE=$(client_list_count)
spawn_xclock "xclock_life" "200x200"
LW=$(wait_for_window "xclock_life") || true

if [[ -n "$LW" ]]; then
    COUNT_AFTER=$(client_list_count)
    if (( COUNT_AFTER > COUNT_BEFORE )); then
        pass "map: client list grows"
    else
        fail "map: client list grows" "before=$COUNT_BEFORE after=$COUNT_AFTER"
    fi
    assert_viewable "map: window is viewable" "$LW"
else
    fail "map: window found" "xclock_life not in tree"
fi

# --- Destroy via _NET_CLOSE_WINDOW: WM removes window ---
if [[ -n "$LW" ]]; then
    COUNT_BEFORE=$(client_list_count)
    D wmctrl -i -c "$LW"

    GONE=0
    for _ in $(seq 1 40); do
        sleep 0.1
        if ! D xwininfo -id "$LW" &>/dev/null 2>&1; then
            GONE=1; break
        fi
    done

    if (( GONE )); then
        pass "destroy: window removed from X tree"
    else
        fail "destroy: window removal" "still exists after 4s"
    fi

    # Remove from spawned list since the window is closed
    SPAWNED_PIDS=("${SPAWNED_PIDS[@]:0:${#SPAWNED_PIDS[@]}-1}")

    COUNT_AFTER=$(client_list_count)
    if (( COUNT_AFTER < COUNT_BEFORE )); then
        pass "destroy: client list shrinks"
    else
        fail "destroy: client list shrinks" "before=$COUNT_BEFORE after=$COUNT_AFTER"
    fi
fi

# --- ConfigureRequest: client-requested geometry applied on floating ---
# Spawn a floating window with specific size
spawn_xclock "xclock_float" "300x250"
FW=$(wait_for_window "xclock_float") || true

if [[ -n "$FW" ]]; then
    sleep 0.3
    FG=$(D xwininfo -id "$FW" 2>/dev/null \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true)
    FWW=$(geom_w "$FG"); FWH=$(geom_h "$FG")

    if [[ -n "$FWW" && -n "$FWH" ]]; then
        # Floating should roughly honor client's 300x250
        assert_approx "configure: floating width" "$FWW" 300 20
        assert_approx "configure: floating height" "$FWH" 250 20
    else
        skip "could not read floating geometry"
    fi
fi

# --- Rapid spawn/destroy churn ---
for round in 1 2 3; do
    PIDS=()
    for i in 1 2 3 4; do
        spawn_xclock "churn_${round}_${i}" "50x50"
    done
    sleep 0.2
    cleanup_spawned
    sleep 0.2
done
assert_alive "rapid spawn/destroy churn doesn't crash"

# --- Window on hidden workspace stays hidden after spawn ---
switch_ws 0
spawn_xclock "xclock_hidden_test" "100x100"
HW=$(wait_for_window "xclock_hidden_test") || true
if [[ -n "$HW" ]]; then
    assert_viewable "window viewable on current ws" "$HW"
    switch_ws 1; sleep 0.1
    assert_not_viewable "window hidden after ws switch" "$HW"
    switch_ws 0; sleep 0.1
    assert_viewable "window reappears after switch back" "$HW"
fi

cleanup_spawned
assert_alive "sirenwm alive after lifecycle tests"
print_summary
(( FAIL == 0 ))
