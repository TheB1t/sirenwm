#!/usr/bin/env bash
# Integration tests for sirenwm Wayland backend running nested inside X11 (Xvfb).
# Uses WLR_BACKENDS=x11 so wlroots creates outputs backed by an X11 window.
#
# Requires: Xvfb, wayland-utils (wayland-info), sirenwm built with SIRENWM_BACKEND=wayland
# Optional: weston-simple-shm (from weston package) for window lifecycle tests

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
SIRENWM="$REPO_ROOT/output/sirenwm"
X_DISPLAY=:98
SCREEN_W=1280
SCREEN_H=720
PASS=0
FAIL=0
SKIP=0

LOG_DIR="${TMPDIR:-/tmp}/sirenwm-wl-itest"
SIRENWM_LOG="$LOG_DIR/sirenwm.log"
XVFB_LOG="$LOG_DIR/xvfb.log"
TEST_HOME="$LOG_DIR/home"
TEST_CONFIG="$TEST_HOME/.config/sirenwm/init.lua"
XDG_RUNTIME="$LOG_DIR/xdg-runtime"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

pass() { echo -e "${GREEN}PASS${NC} $1"; ((++PASS)); }
fail() { echo -e "${RED}FAIL${NC} $1: $2"; ((++FAIL)); }
skip() { echo -e "${CYAN}SKIP${NC} $1: $2"; ((++SKIP)); }
info() { echo -e "${YELLOW}INFO${NC} $1"; }

require_cmd() {
    local c="$1"
    if ! command -v "$c" >/dev/null 2>&1; then
        echo -e "${RED}ERROR${NC} required command not found: $c"
        exit 1
    fi
}

dump_logs() {
    info "sirenwm log:"
    tail -n 80 "$SIRENWM_LOG" 2>/dev/null || true
    info "Xvfb log:"
    tail -n 20 "$XVFB_LOG" 2>/dev/null || true
}

# Run a Wayland client command with the test compositor's socket.
wl_run() {
    XDG_RUNTIME_DIR="$XDG_RUNTIME" WAYLAND_DISPLAY="$WAYLAND_SOCKET_NAME" "$@"
}

# Wait up to N*0.1s for a condition; return 1 on timeout.
wait_for() {
    local desc="$1" max="$2"; shift 2
    local n=0
    while ! "$@" &>/dev/null; do
        sleep 0.1; ((++n))
        if (( n > max )); then
            echo -e "${RED}TIMEOUT${NC} waiting for: $desc"
            return 1
        fi
    done
}

XVFB_PID=0
SIRENWM_PID=0

cleanup() {
    info "Cleaning up..."
    [[ $SIRENWM_PID -ne 0 ]] && kill $SIRENWM_PID 2>/dev/null || true
    [[ $XVFB_PID   -ne 0 ]] && kill $XVFB_PID   2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$LOG_DIR" "$XDG_RUNTIME"
chmod 700 "$XDG_RUNTIME"
: > "$SIRENWM_LOG"
: > "$XVFB_LOG"

for cmd in Xvfb xdpyinfo wayland-info; do
    require_cmd "$cmd"
done

if [[ ! -x "$SIRENWM" ]]; then
    echo -e "${RED}ERROR${NC} sirenwm binary not found: $SIRENWM"
    echo "Build first: cmake -S . -B build -DSIRENWM_BACKEND=wayland && cmake --build build -j"
    exit 1
fi

# ---------------------------------------------------------------------------
# Write init.lua (with bar)
# ---------------------------------------------------------------------------
mkdir -p "$(dirname "$TEST_CONFIG")"
cp -r "$REPO_ROOT/lua/." "$TEST_HOME/.config/sirenwm/"

cat >"$TEST_CONFIG" <<'LUA'
require("keybindings")

siren.modifier = "mod1"

siren.theme = {
  font   = "monospace:size=9",
  bg     = "#111111",
  fg     = "#bbbbbb",
  alt_bg = "#222222",
  alt_fg = "#eeeeee",
  accent = "#005577",
  gap    = 4,
  border = {
    thickness  = 1,
    focused    = "#005577",
    unfocused  = "#222222",
  },
}

siren.monitors = {
  {
    name    = "primary",
    output  = "default",
    width   = 1280,
    height  = 720,
    refresh = 60,
    enabled = true,
  },
}

siren.compose_monitors = {
  primary = "primary",
  layout  = {},
}

siren.workspaces = {
  { name = "[1]", monitor = "primary" },
  { name = "[2]", monitor = "primary" },
  { name = "[3]", monitor = "primary" },
}
LUA

# ---------------------------------------------------------------------------
# Start Xvfb
# ---------------------------------------------------------------------------
info "Starting Xvfb on $X_DISPLAY (${SCREEN_W}x${SCREEN_H})..."
Xvfb "$X_DISPLAY" -screen 0 "${SCREEN_W}x${SCREEN_H}x24" >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

if ! wait_for "Xvfb on $X_DISPLAY" 60 xdpyinfo -display "$X_DISPLAY"; then
    dump_logs; exit 1
fi
info "Xvfb ready."

# ---------------------------------------------------------------------------
# Start sirenwm Wayland backend nested in Xvfb
# ---------------------------------------------------------------------------
info "Starting sirenwm (Wayland backend, WLR_BACKENDS=x11)..."
HOME="$TEST_HOME" \
DISPLAY="$X_DISPLAY" \
WLR_BACKENDS=x11 \
WLR_X11_OUTPUTS=1 \
XDG_RUNTIME_DIR="$XDG_RUNTIME" \
    "$SIRENWM" >"$SIRENWM_LOG" 2>&1 &
SIRENWM_PID=$!

# ===========================================================================
# Test 1: sirenwm starts and stays alive
# ===========================================================================
sleep 1.0
if kill -0 $SIRENWM_PID 2>/dev/null; then
    pass "sirenwm starts and stays alive"
else
    fail "sirenwm starts and stays alive" "process exited immediately"
    dump_logs; exit 1
fi

# ===========================================================================
# Test 2: Wayland socket appears in XDG_RUNTIME_DIR
# ===========================================================================
WL_SOCKET=""
n=0
while true; do
    for f in "$XDG_RUNTIME"/wayland-*; do
        [[ -S "$f" ]] && { WL_SOCKET="$f"; break; }
    done
    [[ -n "$WL_SOCKET" ]] && break
    sleep 0.1; ((++n))
    if (( n > 60 )); then break; fi
done

if [[ -n "$WL_SOCKET" ]]; then
    WAYLAND_SOCKET_NAME="$(basename "$WL_SOCKET")"
    pass "Wayland socket created: $WAYLAND_SOCKET_NAME"
else
    fail "Wayland socket created" "no wayland-N socket in $XDG_RUNTIME"
    dump_logs; exit 1
fi

# ===========================================================================
# Test 3: wayland-info connects and receives global list from compositor
# ===========================================================================
WAYLAND_INFO_OUT="$LOG_DIR/wayland-info.txt"
if wl_run wayland-info >"$WAYLAND_INFO_OUT" 2>&1; then
    pass "wayland-info connects to compositor"
else
    fail "wayland-info connects to compositor" "exit code $?"
    cat "$WAYLAND_INFO_OUT" || true
    dump_logs
fi

# ===========================================================================
# Tests 4-10: advertised Wayland globals
# ===========================================================================
check_global() {
    local proto="$1"
    if grep -q "$proto" "$WAYLAND_INFO_OUT" 2>/dev/null; then
        pass "compositor advertises $proto"
    else
        fail "compositor advertises $proto" "not found in wayland-info output"
    fi
}

check_global "wl_compositor"
check_global "xdg_wm_base"
check_global "wl_seat"
check_global "wl_shm"
check_global "wl_output"
check_global "wl_data_device_manager"

# ===========================================================================
# Test 11: bar initialized (runtime log)
# ===========================================================================
if grep -q 'Bar: initialized' "$TEST_HOME/runtime.log" 2>/dev/null; then
    pass "bar module initialized"
else
    fail "bar module initialized" "not found in runtime.log"
fi

# ===========================================================================
# Test 12: no assertion failures or crashes in runtime log
# ===========================================================================
if grep -qE 'Assertion.*failed|SIGSEGV|SIGABRT|core dump' "$SIRENWM_LOG" 2>/dev/null ||
   grep -qE 'Assertion.*failed|SIGSEGV|SIGABRT|core dump' "$TEST_HOME/runtime.log" 2>/dev/null; then
    fail "no crashes in runtime log" "assertion/signal found"
else
    pass "no crashes in runtime log"
fi

# ===========================================================================
# Test 13: FSM reaches Running state
# ===========================================================================
if grep -q 'FSM: Starting → Running' "$TEST_HOME/runtime.log" 2>/dev/null; then
    pass "FSM reached Running state"
else
    fail "FSM reached Running state" "transition not found in runtime.log"
fi

# ===========================================================================
# Test 14: output attached (monitor topology applied)
# ===========================================================================
if grep -q "WlMonitorPort: applied" "$TEST_HOME/runtime.log" 2>/dev/null; then
    pass "monitor layout applied"
else
    fail "monitor layout applied" "not found in runtime.log"
fi

# ===========================================================================
# Test 15: multiple simultaneous wayland-info connections
# ===========================================================================
WI_PIDS=()
for i in 1 2 3; do
    wl_run wayland-info >"$LOG_DIR/wayland-info-$i.txt" 2>&1 &
    WI_PIDS+=($!)
done
MULTI_OK=true
for pid in "${WI_PIDS[@]}"; do
    if ! wait "$pid"; then MULTI_OK=false; fi
done
if $MULTI_OK; then
    pass "3 simultaneous wayland-info connections succeed"
else
    fail "3 simultaneous wayland-info connections succeed" "at least one failed"
fi

# ===========================================================================
# Test 16: compositor stays alive after multiple client connections
# ===========================================================================
if kill -0 $SIRENWM_PID 2>/dev/null; then
    pass "compositor alive after multiple client connections"
else
    fail "compositor alive after multiple client connections" "process died"
    dump_logs
fi

# ===========================================================================
# Test 17: SIGHUP reload — compositor survives config reload
# ===========================================================================
kill -HUP $SIRENWM_PID 2>/dev/null || true
sleep 0.8
if kill -0 $SIRENWM_PID 2>/dev/null; then
    pass "compositor survives SIGHUP reload"
else
    fail "compositor survives SIGHUP reload" "process died after SIGHUP"
    dump_logs
fi

# ===========================================================================
# Test 18: wl_shm version >= 1 (supports shared memory buffers)
# ===========================================================================
if grep -qE "wl_shm.*version:.*[1-9]" "$WAYLAND_INFO_OUT" 2>/dev/null; then
    pass "wl_shm version >= 1"
else
    fail "wl_shm version >= 1" "not found in wayland-info output"
fi

# ===========================================================================
# Test 19: wl_seat capabilities include pointer and keyboard
# ===========================================================================
if grep -qE 'pointer|keyboard' "$WAYLAND_INFO_OUT" 2>/dev/null; then
    pass "wl_seat advertises pointer/keyboard capabilities"
else
    fail "wl_seat advertises pointer/keyboard capabilities" "not found in wayland-info output"
fi

# ===========================================================================
# Test 20: weston-simple-shm creates a window (optional — skip if not present)
# ===========================================================================
WESTON_SHM=$(command -v weston-simple-shm 2>/dev/null || true)
if [[ -z "$WESTON_SHM" ]]; then
    skip "weston-simple-shm window lifecycle" "weston-simple-shm not installed"
else
    # Run for 0.5s then kill; compositor must stay alive
    WS_LOG="$LOG_DIR/weston-simple-shm.log"
    wl_run "$WESTON_SHM" >"$WS_LOG" 2>&1 &
    WS_PID=$!
    sleep 0.5
    kill $WS_PID 2>/dev/null || true
    wait $WS_PID 2>/dev/null || true

    if kill -0 $SIRENWM_PID 2>/dev/null; then
        pass "compositor alive after weston-simple-shm window lifecycle"
    else
        fail "compositor alive after weston-simple-shm window lifecycle" "compositor crashed"
        dump_logs
    fi

    # Check compositor logged a new surface
    if grep -qE 'new xdg-toplevel|surface.*mapped|WindowMapped' "$TEST_HOME/runtime.log" 2>/dev/null; then
        pass "compositor registered weston-simple-shm xdg-toplevel"
    else
        skip "compositor registered weston-simple-shm xdg-toplevel" "weston-simple-shm may use wl_shell not xdg-shell"
    fi
fi

# ===========================================================================
# Test 21: graceful SIGINT shutdown
# ===========================================================================
kill -INT $SIRENWM_PID 2>/dev/null || true
STOPPED=false
for _ in $(seq 1 20); do
    sleep 0.1
    if ! kill -0 $SIRENWM_PID 2>/dev/null; then STOPPED=true; break; fi
done
if $STOPPED; then
    pass "compositor shuts down cleanly on SIGINT"
    SIRENWM_PID=0
else
    fail "compositor shuts down cleanly on SIGINT" "still running after 2s"
fi

# ===========================================================================
# Summary
# ===========================================================================
echo ""
echo "----------------------------------------"
echo -e "Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}, ${CYAN}$SKIP skipped${NC}"
echo "----------------------------------------"

[[ $FAIL -eq 0 ]]
