#!/usr/bin/env bash
# Close tests: _NET_CLOSE_WINDOW, rapid close, client list shrinks.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

switch_ws 0

# --- Single close removes from tree and client list ---
spawn_xclock "xclock_close" "100x100"
CLOSE_WIN=$(wait_for_window "xclock_close") || true

if [[ -n "$CLOSE_WIN" ]]; then
    pass "close-test xclock spawned ($CLOSE_WIN)"

    PRE=$(root_prop _NET_CLIENT_LIST)
    WIN_HEX=$(echo "$CLOSE_WIN" | tr 'A-F' 'a-f')
    assert_contains "window in client list before close" "$PRE" "$WIN_HEX"

    D wmctrl -i -c "$CLOSE_WIN"

    GONE=0
    for _ in $(seq 1 40); do
        sleep 0.1
        if ! D xwininfo -id "$CLOSE_WIN" &>/dev/null 2>&1; then
            GONE=1; break
        fi
    done

    if (( GONE )); then
        pass "closed window removed from X tree"
    else
        fail "closed window removed from X tree" "still exists after 4s"
    fi

    POST=$(root_prop _NET_CLIENT_LIST)
    assert_not_contains "closed window removed from client list" "$POST" "$WIN_HEX"
else
    fail "close-test xclock spawned" "window not found"
fi

# --- Close shrinks client list count ---
spawn_xclock "xclock_shrink" "100x100"
SH_WIN=$(wait_for_window "xclock_shrink") || true
if [[ -n "$SH_WIN" ]]; then
    COUNT_BEFORE=$(client_list_count)
    D wmctrl -i -c "$SH_WIN"
    sleep 0.5
    COUNT_AFTER=$(client_list_count)
    assert_lt "client list shrinks after close" "$COUNT_AFTER" "$COUNT_BEFORE"
fi

# --- Multiple rapid closes ---
for name in xclock_mc1 xclock_mc2 xclock_mc3; do
    spawn_xclock "$name"
done
sleep 0.4

MC_WINS=()
for name in xclock_mc1 xclock_mc2 xclock_mc3; do
    wid=$(get_window_id "$name")
    [[ -n "$wid" ]] && MC_WINS+=("$wid")
done

if (( ${#MC_WINS[@]} == 3 )); then
    for wid in "${MC_WINS[@]}"; do
        D wmctrl -i -c "$wid"
    done
    sleep 1.0

    ALL_GONE=true
    for wid in "${MC_WINS[@]}"; do
        if D xwininfo -id "$wid" &>/dev/null 2>&1; then
            ALL_GONE=false; break
        fi
    done

    if $ALL_GONE; then
        pass "multiple rapid close: all windows removed"
    else
        fail "multiple rapid close" "some windows still exist"
    fi
else
    skip "could not spawn 3 windows for rapid close (got ${#MC_WINS[@]})"
fi

cleanup_spawned
assert_alive "sirenwm alive after close tests"
print_summary
(( FAIL == 0 ))
