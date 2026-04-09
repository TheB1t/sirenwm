#!/usr/bin/env bash
# Integration tests for sirenwm Wayland backend running nested inside X11 (Xvfb).
# Uses WLR_BACKENDS=x11 so wlroots creates outputs backed by an X11 window.
#
# Requires: Xvfb, wayland-utils (wayland-info), sirenwm built with SIRENWM_BACKEND=wayland

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
SIRENWM="$REPO_ROOT/output/sirenwm"
X_DISPLAY=:98
SCREEN_W=1280
SCREEN_H=720
PASS=0
FAIL=0

LOG_DIR="${TMPDIR:-/tmp}/sirenwm-wl-itest"
SIRENWM_LOG="$LOG_DIR/sirenwm.log"
XVFB_LOG="$LOG_DIR/xvfb.log"
TEST_HOME="$LOG_DIR/home"
TEST_CONFIG="$TEST_HOME/.config/sirenwm/init.lua"
XDG_RUNTIME="$LOG_DIR/xdg-runtime"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "${GREEN}PASS${NC} $1"; ((++PASS)); }
fail() { echo -e "${RED}FAIL${NC} $1: $2"; ((++FAIL)); }
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

for cmd in Xvfb wayland-info; do
    require_cmd "$cmd"
done

if [[ ! -x "$SIRENWM" ]]; then
    echo -e "${RED}ERROR${NC} sirenwm binary not found: $SIRENWM"
    echo "Build first: cmake -S . -B build -DSIRENWM_BACKEND=wayland && cmake --build build -j"
    exit 1
fi

# ---------------------------------------------------------------------------
# Write minimal init.lua (Wayland — no X11-specific things like xwallpaper)
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
    name     = "primary",
    output   = "default",
    width    = 1280,
    height   = 720,
    refresh  = 60,
    rotation = "normal",
    enabled  = true,
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

# Wait for Xvfb to be ready
n=0
while ! DISPLAY="$X_DISPLAY" xdpyinfo &>/dev/null; do
    sleep 0.1
    ((++n))
    if (( n > 60 )); then
        echo -e "${RED}TIMEOUT${NC} waiting for Xvfb on $X_DISPLAY"
        dump_logs; exit 1
    fi
done
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

# ---------------------------------------------------------------------------
# Test 1: sirenwm starts and stays alive
# ---------------------------------------------------------------------------
sleep 1.0
if kill -0 $SIRENWM_PID 2>/dev/null; then
    pass "sirenwm starts and stays alive"
else
    fail "sirenwm starts and stays alive" "process exited immediately"
    dump_logs
    exit 1
fi

# ---------------------------------------------------------------------------
# Test 2: Wayland socket appears in XDG_RUNTIME_DIR
# ---------------------------------------------------------------------------
WL_SOCKET=""
n=0
while true; do
    for f in "$XDG_RUNTIME"/wayland-*; do
        if [[ -S "$f" ]]; then
            WL_SOCKET="$f"
            break
        fi
    done
    [[ -n "$WL_SOCKET" ]] && break
    sleep 0.1
    ((++n))
    if (( n > 60 )); then break; fi
done

if [[ -n "$WL_SOCKET" ]]; then
    WAYLAND_SOCKET_NAME="$(basename "$WL_SOCKET")"
    pass "Wayland socket created: $WAYLAND_SOCKET_NAME"
else
    fail "Wayland socket created" "no wayland-N socket in $XDG_RUNTIME"
    dump_logs
    exit 1
fi

# ---------------------------------------------------------------------------
# Test 3: wayland-info connects and receives global list from compositor
# ---------------------------------------------------------------------------
WAYLAND_INFO_OUT="$LOG_DIR/wayland-info.txt"
if XDG_RUNTIME_DIR="$XDG_RUNTIME" \
   WAYLAND_DISPLAY="$WAYLAND_SOCKET_NAME" \
   wayland-info >"$WAYLAND_INFO_OUT" 2>&1; then
    pass "wayland-info connects to compositor"
else
    fail "wayland-info connects to compositor" "exit code $?"
    cat "$WAYLAND_INFO_OUT" || true
    dump_logs
fi

# ---------------------------------------------------------------------------
# Test 4: compositor advertises wl_compositor
# ---------------------------------------------------------------------------
if grep -q 'wl_compositor' "$WAYLAND_INFO_OUT" 2>/dev/null; then
    pass "compositor advertises wl_compositor"
else
    fail "compositor advertises wl_compositor" "not found in wayland-info output"
fi

# ---------------------------------------------------------------------------
# Test 5: compositor advertises xdg_wm_base (XDG shell)
# ---------------------------------------------------------------------------
if grep -q 'xdg_wm_base' "$WAYLAND_INFO_OUT" 2>/dev/null; then
    pass "compositor advertises xdg_wm_base"
else
    fail "compositor advertises xdg_wm_base" "not found in wayland-info output"
fi

# ---------------------------------------------------------------------------
# Test 6: compositor advertises wl_seat
# ---------------------------------------------------------------------------
if grep -q 'wl_seat' "$WAYLAND_INFO_OUT" 2>/dev/null; then
    pass "compositor advertises wl_seat"
else
    fail "compositor advertises wl_seat" "not found in wayland-info output"
fi

# ---------------------------------------------------------------------------
# Test 7: sirenwm is still alive after clients connected
# ---------------------------------------------------------------------------
if kill -0 $SIRENWM_PID 2>/dev/null; then
    pass "sirenwm stays alive after client connections"
else
    fail "sirenwm stays alive after client connections" "process died"
    dump_logs
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "----------------------------------------"
echo -e "Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}"
echo "----------------------------------------"

[[ $FAIL -eq 0 ]]
