#!/usr/bin/env bash
# Shared setup/teardown for integration test suites.
# Source this first; it starts Xephyr + sirenwm and sets up cleanup traps.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LIB_DIR="$SCRIPT_DIR"
INTEGRATION_DIR="$(cd -- "$LIB_DIR/.." && pwd)"
REPO_ROOT="$(cd -- "$INTEGRATION_DIR/../.." && pwd)"
SIRENWM="$REPO_ROOT/output/sirenwm"
DISPLAY_NUM="${DISPLAY_NUM:-:99}"
SCREEN_W=1280
SCREEN_H=720
PASS=0
FAIL=0

LOG_DIR="${TMPDIR:-/tmp}/sirenwm-itest"
SIRENWM_LOG="$LOG_DIR/sirenwm.log"
XEPHYR_LOG="$LOG_DIR/xephyr.log"
TEST_HOME="$LOG_DIR/home"
TEST_CONFIG="$TEST_HOME/.config/sirenwm/init.lua"

SPAWNED_PIDS=()

source "$LIB_DIR/assertions.sh"
source "$LIB_DIR/helpers.sh"

require_cmd() {
    local c="$1"
    if ! command -v "$c" >/dev/null 2>&1; then
        echo -e "${RED}ERROR${NC} required command not found: $c"
        exit 1
    fi
}

XEPHYR_PID=0
SIRENWM_PID=0

cleanup() {
    cleanup_spawned
    [[ $SIRENWM_PID -ne 0 ]] && kill $SIRENWM_PID 2>/dev/null || true
    [[ $XEPHYR_PID -ne 0 ]]  && kill $XEPHYR_PID  2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

start_xephyr() {
    mkdir -p "$LOG_DIR"
    : > "$SIRENWM_LOG"
    : > "$XEPHYR_LOG"

    for cmd in Xephyr xdpyinfo xwininfo xprop xclock wmctrl; do
        require_cmd "$cmd"
    done

    if [[ ! -x "$SIRENWM" ]]; then
        echo -e "${RED}ERROR${NC} sirenwm binary not found: $SIRENWM"
        echo "Build first: cmake -S . -B build && cmake --build build -j"
        exit 1
    fi

    HOST_DISPLAY=${DISPLAY:-:0}
    info "Starting Xephyr on $DISPLAY_NUM (${SCREEN_W}x${SCREEN_H}), host=$HOST_DISPLAY..."
    DISPLAY=$HOST_DISPLAY Xephyr $DISPLAY_NUM -screen ${SCREEN_W}x${SCREEN_H} -noreset >"$XEPHYR_LOG" 2>&1 &
    XEPHYR_PID=$!
    if ! wait_for_display $DISPLAY_NUM; then
        dump_logs; exit 1
    fi
}

write_config() {
    mkdir -p "$(dirname "$TEST_CONFIG")"
    cat >"$TEST_CONFIG" <<'LUA'
require("keybindings")
local bar = require("bar")

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

siren.mouse = {
  { "mod1+Button1", "move"   },
  { "mod1+Button3", "resize" },
  { "mod1+Button2", "float"  },
}

siren.layout = {
  name          = "tile",
  master_factor = 0.55,
}

bar.settings = {
  top = {
    height = 18,
    left   = { siren.load("widgets.tags") },
    center = { siren.load("widgets.title") },
    right  = { siren.load("widgets.tray") },
  },
}

siren.on("window.rules", function(win)
  if win.from_restart then return end
  if win.class == "XTerm" then
    siren.win.move_to(win.id, 2)
  elseif win.class == "XClock" and win.instance == "xclock_float" then
    siren.win.set_floating(win.id, true)
  end
end)

siren.binds = {
  { "mod1+j", function() siren.win.focus_next() end },
  { "mod1+k", function() siren.win.focus_prev() end },
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

    # Install bundled Lua modules
    cp -r "$REPO_ROOT/lua/." "$TEST_HOME/.config/sirenwm/"

    # Mock xwallpaper
    WALLPAPER_ARGS_FILE="$LOG_DIR/xwallpaper_args.txt"
    mkdir -p "$TEST_HOME/bin"
    cat >"$TEST_HOME/bin/xwallpaper" <<MOCK
#!/bin/sh
echo "\$@" > "$WALLPAPER_ARGS_FILE"
MOCK
    chmod +x "$TEST_HOME/bin/xwallpaper"
    export PATH="$TEST_HOME/bin:$PATH"

    # Wallpaper config (needs variable expansion)
    cat >>"$TEST_CONFIG" <<LUA

local wallpaper = require("swm.wallpaper")
wallpaper.settings = {
  primary = { image = "$INTEGRATION_DIR/picture.png", mode = "stretch" },
}
LUA
}

start_sirenwm() {
    info "Starting sirenwm..."
    HOME=$TEST_HOME DISPLAY=$DISPLAY_NUM PATH="$TEST_HOME/bin:$PATH" "$SIRENWM" >"$SIRENWM_LOG" 2>&1 &
    SIRENWM_PID=$!
    sleep 0.8

    if ! kill -0 $SIRENWM_PID 2>/dev/null; then
        fail "sirenwm starts" "process exited immediately"
        dump_logs
        exit 1
    fi
}

print_summary() {
    printf "\nResults: %b%d passed%b, %b%d failed%b\n\n" \
        "$GREEN" "$PASS" "$NC" "$RED" "$FAIL" "$NC"
    if (( FAIL > 0 )); then
        info "Logs kept in $LOG_DIR"
    fi
}
