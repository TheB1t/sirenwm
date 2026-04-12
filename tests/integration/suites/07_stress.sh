#!/usr/bin/env bash
# Stress tests: rapid ops, bulk window churn.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

# --- Rapid workspace switching ---
for ws_idx in 0 1 2 0 2 1 0 1 2 0; do
    D wmctrl -s $ws_idx
    sleep 0.05
done
switch_ws 0; sleep 0.2
assert_alive "rapid workspace switching doesn't crash"

# --- Rapid window open/close ---
RAPID_PIDS=()
for i in $(seq 1 8); do
    spawn_xclock "rapid_$i" "50x50"
done
sleep 0.3
cleanup_spawned; sleep 0.5
assert_alive "rapid window open/close doesn't crash"

# --- Interleaved spawn/switch/close ---
for round in 1 2 3; do
    spawn_xclock "churn_${round}_a"
    spawn_xclock "churn_${round}_b"
    sleep 0.1
    switch_ws $(( round % 3 ))
    sleep 0.1
done
cleanup_spawned; sleep 0.3
switch_ws 0
assert_alive "interleaved spawn/switch/close doesn't crash"

# --- Bulk client list stress ---
for i in $(seq 1 15); do
    spawn_xclock "bulk_$i" "30x30"
done
sleep 2

COUNT=$(client_list_count)
if (( COUNT >= 15 )); then
    pass "bulk spawn: $COUNT windows in client list"
else
    fail "bulk spawn" "only $COUNT windows, expected >=15"
fi

cleanup_spawned; sleep 0.5
assert_alive "sirenwm alive after stress tests"
print_summary
(( FAIL == 0 ))
