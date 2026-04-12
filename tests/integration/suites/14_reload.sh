#!/usr/bin/env bash
# Reload and exec-restart tests.

source "$(dirname "$0")/../lib/setup.sh"
start_xephyr
write_config
start_sirenwm

# --- SIGHUP reload: config is re-applied ---
LOG_BEFORE=$(wc -l <"$SIRENWM_LOG" 2>/dev/null || echo 0)

kill -HUP "$SIRENWM_PID" 2>/dev/null || true
sleep 1.0

RELOAD_OK=0
tail -n +"$LOG_BEFORE" "$SIRENWM_LOG" 2>/dev/null | grep -q "reload" && RELOAD_OK=1

if (( RELOAD_OK )); then
    pass "SIGHUP triggers config reload"
else
    fail "SIGHUP reload" "no reload log entry found"
fi

assert_alive "sirenwm alive after SIGHUP"

# Spawn a window to verify WM still works after reload
spawn_xclock "xclock_reload" "200x200"
RW=$(wait_for_window "xclock_reload") || true
if [[ -n "$RW" ]]; then
    assert_viewable "window works after reload" "$RW"
else
    fail "window after reload" "not found"
fi
cleanup_spawned

# --- exec-restart: siren.restart() ---
RESTART_LUA="$TEST_HOME/.config/sirenwm/init.lua"
cp "$RESTART_LUA" "${RESTART_LUA}.bak"
cat >>"$RESTART_LUA" <<'LUA'
siren.on("wm.reloaded", function() siren.restart() end)
LUA

LOG_BEFORE=$(wc -l <"$SIRENWM_LOG" 2>/dev/null || echo 0)

kill -HUP "$SIRENWM_PID" 2>/dev/null || true

RESTART_HAPPENED=0
for _ in $(seq 1 50); do
    sleep 0.1
    if tail -n +"$LOG_BEFORE" "$SIRENWM_LOG" 2>/dev/null \
            | grep -q "restart: replacing process"; then
        RESTART_HAPPENED=1; break
    fi
done

# Restore config immediately
cp "${RESTART_LUA}.bak" "$RESTART_LUA"
rm -f "${RESTART_LUA}.bak"

if (( RESTART_HAPPENED )); then
    pass "exec-restart triggered (execv log line seen)"
else
    fail "exec-restart triggered" "log line never appeared"
fi

# PID stays the same after execv. Wait for FSM Running.
RESTART_RUNNING=0
for _ in $(seq 1 40); do
    sleep 0.1
    COUNT=$(grep -c "FSM: Starting → Running" "$SIRENWM_LOG" 2>/dev/null || true)
    if [[ $COUNT -ge 2 ]]; then
        RESTART_RUNNING=1; break
    fi
done

if (( RESTART_RUNNING )); then
    pass "exec-restart: WM reached Running state after restart"
else
    fail "exec-restart: Running state" "FSM Running not seen twice"
    dump_logs
fi

sleep 0.3
assert_alive "exec-restart: no segfault (process alive)"

# Verify WM works after restart
spawn_xclock "xclock_post_restart" "200x200"
PRW=$(wait_for_window "xclock_post_restart") || true
if [[ -n "$PRW" ]]; then
    assert_viewable "window works after exec-restart" "$PRW"
else
    fail "window after restart" "not found"
fi

cleanup_spawned
assert_alive "sirenwm alive after reload tests"
print_summary
(( FAIL == 0 ))
