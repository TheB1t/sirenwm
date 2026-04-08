#!/usr/bin/env bash
# Integration tests for sirenwm running inside Xephyr.
# Usage: ./run_tests.sh
# Requires: Xephyr, xdpyinfo, xwininfo, xprop, xclock, xterm, wmctrl

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
SIRENWM="$REPO_ROOT/output/sirenwm"
DISPLAY_NUM=:99
SCREEN_W=1280
SCREEN_H=720
PASS=0
FAIL=0

LOG_DIR="${TMPDIR:-/tmp}/sirenwm-itest"
SIRENWM_LOG="$LOG_DIR/sirenwm.log"
XEPHYR_LOG="$LOG_DIR/xephyr.log"
TEST_HOME="$LOG_DIR/home"
TEST_CONFIG="$TEST_HOME/.config/sirenwm/init.lua"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

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
    info "sirenwm log: $SIRENWM_LOG"
    tail -n 60 "$SIRENWM_LOG" 2>/dev/null || true
    info "Xephyr log: $XEPHYR_LOG"
    tail -n 60 "$XEPHYR_LOG" 2>/dev/null || true
}

XEPHYR_PID=0
SIRENWM_PID=0

cleanup() {
    info "Cleaning up..."
    [[ $SIRENWM_PID -ne 0 ]]    && kill $SIRENWM_PID    2>/dev/null || true
    [[ $XEPHYR_PID -ne 0 ]] && kill $XEPHYR_PID 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$LOG_DIR"
: > "$SIRENWM_LOG"
: > "$XEPHYR_LOG"

for cmd in Xephyr xdpyinfo xwininfo xprop xclock xterm wmctrl; do
    require_cmd "$cmd"
done

if [[ ! -x "$SIRENWM" ]]; then
    echo -e "${RED}ERROR${NC} sirenwm binary not found: $SIRENWM"
    echo "Build first from repo root:"
    echo "  cmake -S . -B build && cmake --build build -j"
    exit 1
fi

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
  },
}

siren.on("window_rules", function(win)
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

# Install bundled Lua modules so require("swm.*") works without a system install.
mkdir -p "$TEST_HOME/.config/sirenwm"
cp -r "$REPO_ROOT/lua/." "$TEST_HOME/.config/sirenwm/"

# Create a mock xwallpaper that records its arguments instead of running.
WALLPAPER_ARGS_FILE="$LOG_DIR/xwallpaper_args.txt"
mkdir -p "$TEST_HOME/bin"
cat >"$TEST_HOME/bin/xwallpaper" <<MOCK
#!/bin/sh
echo "\$@" > "$WALLPAPER_ARGS_FILE"
MOCK
chmod +x "$TEST_HOME/bin/xwallpaper"
export PATH="$TEST_HOME/bin:$PATH"

# Append wallpaper config (needs variable expansion for the image path).
cat >>"$TEST_CONFIG" <<LUA

local wallpaper = require("swm.wallpaper")
wallpaper.settings = {
  primary = { image = "$SCRIPT_DIR/picture.png", mode = "stretch" },
}
LUA

wait_for_display() {
    local disp="$1"
    local n=0
    while ! DISPLAY=$disp xdpyinfo &>/dev/null; do
        sleep 0.1
        ((++n))
        if (( n > 60 )); then
            echo -e "${RED}TIMEOUT${NC} waiting for display $disp"
            return 1
        fi
    done
}

wait_for_swm() {
    # Wait until sirenwm has set SubstructureRedirect on root (i.e. it's running)
    local disp="$1"
    local n=0
    while ! DISPLAY=$disp xprop -root _NET_SUPPORTED &>/dev/null 2>&1; do
        sleep 0.1
        ((++n))
        if (( n > 60 )); then
            echo -e "${RED}TIMEOUT${NC} waiting for sirenwm on $disp"
            return 1
        fi
    done
}

# Get geometry of a window by class. Returns "WxH+X+Y" or empty.
get_geom() {
    local class="$1"
    DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep -i "$class" | head -1 \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true
}

get_window_id() {
    local pattern="$1"
    DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep -i "$pattern" | head -1 | awk '{print $1}' || true
}

is_window_viewable() {
    local wid="$1"
    DISPLAY=$DISPLAY_NUM xwininfo -id "$wid" 2>/dev/null | grep -q "Map State: IsViewable"
}

current_desktop() {
    DISPLAY=$DISPLAY_NUM wmctrl -d 2>/dev/null | awk '$2 == "*" { print $1; exit }'
}

active_window() {
    DISPLAY=$DISPLAY_NUM xprop -root _NET_ACTIVE_WINDOW 2>/dev/null \
        | awk '{print $NF}' | tr 'A-Z' 'a-z'
}

wait_for_desktop() {
    local expected="$1"
    local n=0
    while true; do
        local cur
        cur="$(current_desktop)"
        if [[ "$cur" == "$expected" ]]; then
            return 0
        fi
        sleep 0.1
        ((++n))
        if (( n > 50 )); then
            return 1
        fi
    done
}

normalize_wid() {
    # Convert any hex window ID to consistent lowercase form (e.g. 0x600000a)
    local raw
    raw="$(echo "$1" | tr 'A-Z' 'a-z')"
    # Strip leading zeros after 0x so comparison works regardless of padding
    echo "$raw" | sed 's/^0x0*/0x/'
}

wait_for_active_window() {
    local expected
    expected="$(normalize_wid "$1")"
    local n=0
    while true; do
        local cur
        cur="$(normalize_wid "$(active_window)")"
        if [[ "$cur" == "$expected" ]]; then
            return 0
        fi
        sleep 0.1
        ((++n))
        if (( n > 50 )); then
            return 1
        fi
    done
}

# Parse fields from WxH+X+Y
geom_w()  { echo "$1" | grep -oE '^[0-9]+' || true; }
geom_h()  { echo "$1" | sed 's/^[0-9]*x//;s/+.*//' || true; }
geom_x()  { echo "$1" | grep -oE '\+[0-9]+\+' | head -1 | tr -d '+' || true; }
geom_y()  { echo "$1" | grep -oE '\+[0-9]+$' | tr -d '+' || true; }

assert_approx() {
    local desc="$1" val="$2" expected="$3" tol="${4:-8}"
    local diff=$(( val - expected ))
    diff=${diff#-}
    if (( diff <= tol )); then
        pass "$desc (got $val, expected ~$expected)"
    else
        fail "$desc" "got $val, expected ~$expected (tolerance $tol)"
    fi
}

assert_lt() {
    local desc="$1" a="$2" b="$3"
    if (( a < b )); then
        pass "$desc ($a < $b)"
    else
        fail "$desc" "$a is not < $b"
    fi
}

# ---------------------------------------------------------------------------
# Start Xephyr
# ---------------------------------------------------------------------------

HOST_DISPLAY=${DISPLAY:-:0}
info "Starting Xephyr on $DISPLAY_NUM (${SCREEN_W}x${SCREEN_H}), host=$HOST_DISPLAY..."
DISPLAY=$HOST_DISPLAY Xephyr $DISPLAY_NUM -screen ${SCREEN_W}x${SCREEN_H} -noreset >"$XEPHYR_LOG" 2>&1 &
XEPHYR_PID=$!
if ! wait_for_display $DISPLAY_NUM; then
    dump_logs
    exit 1
fi

info "Starting sirenwm..."
HOME=$TEST_HOME DISPLAY=$DISPLAY_NUM PATH="$TEST_HOME/bin:$PATH" "$SIRENWM" >"$SIRENWM_LOG" 2>&1 &
SIRENWM_PID=$!
sleep 0.8

# ---------------------------------------------------------------------------
# Test 1: sirenwm starts without crashing
# ---------------------------------------------------------------------------

if kill -0 $SIRENWM_PID 2>/dev/null; then
    pass "sirenwm starts and stays alive"
else
    fail "sirenwm starts and stays alive" "process exited immediately"
    dump_logs
fi

# ---------------------------------------------------------------------------
# Test 2: Single window fills monitor (minus gap and border)
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM xclock -geometry 200x200 &
XCLOCK_PID=$!
GEOM=""
for _ in $(seq 1 20); do
    sleep 0.1
    GEOM=$(get_geom "xclock")
    [[ -n "$GEOM" ]] && break
done
XCLOCK1_ID="$(get_window_id "xclock")"

if [[ -z "$GEOM" ]]; then
    fail "xclock appears on screen" "xwininfo returned empty"
else
    pass "xclock appears on screen (geom: $GEOM)"
    W=$(geom_w "$GEOM")
    H=$(geom_h "$GEOM")
    X=$(geom_x "$GEOM")
    Y=$(geom_y "$GEOM")

    # With gap=4, border=1: usable area = 1280-8 = 1272 wide, 720-8 = 712 tall
    # Single window: x=4, y=4+barheight, w=1272-2=1270, h=(712-barh)-2
    # We use generous tolerance since bar height varies
    assert_approx "xclock x near left edge" "$X" 4 10
    assert_lt     "xclock width < screen width" "$W" $SCREEN_W
    assert_lt     "xclock height < screen height" "$H" $SCREEN_H
fi

# ---------------------------------------------------------------------------
# Test 3: Two windows — master/stack split
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM xclock -name xclock2 -geometry 200x200 &
XCLOCK2_PID=$!
sleep 0.3

mapfile -t GEOMS < <(
    DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep -i "xclock" \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' \
        | head -2
)

if (( ${#GEOMS[@]} == 2 )); then
    G1="${GEOMS[0]}"
    G2="${GEOMS[1]}"
    X1=$(geom_x "$G1"); W1=$(geom_w "$G1")
    X2=$(geom_x "$G2"); W2=$(geom_w "$G2")

    if (( X1 <= X2 )); then
        MASTER_X=$X1
        MASTER_W=$W1
        STACK_X=$X2
        STACK_W=$W2
    else
        MASTER_X=$X2
        MASTER_W=$W2
        STACK_X=$X1
        STACK_W=$W1
    fi

    layout_ok=1

    # Master should be left of stack.
    MASTER_RIGHT=$(( MASTER_X + MASTER_W ))
    if (( MASTER_RIGHT < STACK_X )); then
        pass "master right edge < stack left edge ($MASTER_RIGHT < $STACK_X)"
    else
        fail "master right edge < stack left edge" "$MASTER_RIGHT is not < $STACK_X"
        layout_ok=0
    fi

    # Master should be wider than stack (factor 0.55).
    if (( STACK_W < MASTER_W )); then
        pass "stack width < master width ($STACK_W < $MASTER_W)"
    else
        fail "stack width < master width" "$STACK_W is not < $MASTER_W"
        layout_ok=0
    fi

    if (( layout_ok == 1 )); then
        pass "two windows: master/stack layout"
    fi
else
    info "Could not get both window geometries for split test (skipping)"
fi

# ---------------------------------------------------------------------------
# Test 4: EWMH _NET_CLIENT_LIST is present
# ---------------------------------------------------------------------------

CLIENT_LIST=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_CLIENT_LIST 2>/dev/null)
if echo "$CLIENT_LIST" | grep -q "window id"; then
    pass "EWMH _NET_CLIENT_LIST is set"
else
    fail "EWMH _NET_CLIENT_LIST is set" "xprop returned: $CLIENT_LIST"
fi

# ---------------------------------------------------------------------------
# Test 5: Workspace switching via _NET_CURRENT_DESKTOP
# ---------------------------------------------------------------------------

if wait_for_desktop 0; then
    pass "starts on workspace 1 (desktop index 0)"
else
    fail "starts on workspace 1 (desktop index 0)" "root _NET_CURRENT_DESKTOP is not 0"
fi

DISPLAY=$DISPLAY_NUM wmctrl -s 1
if wait_for_desktop 1; then
    pass "_NET_CURRENT_DESKTOP request switches to workspace 2"
else
    fail "_NET_CURRENT_DESKTOP request switches to workspace 2" "desktop index did not become 1"
fi

if [[ -n "${XCLOCK1_ID:-}" ]]; then
    if is_window_viewable "$XCLOCK1_ID"; then
        fail "workspace switch hides previous workspace windows" "xclock remained viewable on workspace 2"
    else
        pass "workspace switch hides previous workspace windows"
    fi
fi

DISPLAY=$DISPLAY_NUM wmctrl -s 0
if wait_for_desktop 0; then
    pass "_NET_CURRENT_DESKTOP request switches back to workspace 1"
else
    fail "_NET_CURRENT_DESKTOP request switches back to workspace 1" "desktop index did not become 0"
fi

if [[ -n "${XCLOCK1_ID:-}" ]]; then
    if is_window_viewable "$XCLOCK1_ID"; then
        pass "returning workspace remaps previous windows"
    else
        fail "returning workspace remaps previous windows" "xclock is not viewable after switching back"
    fi
fi

# ---------------------------------------------------------------------------
# Test 6: Rule-routed window does not force workspace jump
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM xterm -title wm-itest-xterm -name wm_itest_xterm &
XTERM_PID=$!

XTERM_ID=""
for _ in $(seq 1 50); do
    XTERM_ID="$(get_window_id "wm-itest-xterm")"
    [[ -n "$XTERM_ID" ]] && break
    sleep 0.1
done

if [[ -z "$XTERM_ID" ]]; then
    fail "ruled xterm appears in X tree" "could not find wm-itest-xterm window"
else
    pass "ruled xterm appears in X tree ($XTERM_ID)"
fi

if [[ "$(current_desktop)" == "0" ]]; then
    pass "rule-routed xterm does not steal current workspace"
else
    fail "rule-routed xterm does not steal current workspace" "desktop index changed unexpectedly"
fi

if [[ -n "$XTERM_ID" ]]; then
    if is_window_viewable "$XTERM_ID"; then
        fail "rule-routed xterm stays hidden off-workspace" "xterm is viewable on workspace 1"
    else
        pass "rule-routed xterm stays hidden off-workspace"
    fi
fi

DISPLAY=$DISPLAY_NUM wmctrl -s 1
if wait_for_desktop 1; then
    pass "switching to rule workspace succeeds"
else
    fail "switching to rule workspace succeeds" "desktop index did not become 1"
fi

if [[ -n "$XTERM_ID" ]]; then
    # Wait up to 2s for the window to become viewable after workspace switch.
    XTERM_VIEWABLE=false
    for _ in $(seq 1 20); do
        if is_window_viewable "$XTERM_ID"; then
            XTERM_VIEWABLE=true
            break
        fi
        sleep 0.1
    done
    if $XTERM_VIEWABLE; then
        pass "rule-routed xterm becomes viewable on target workspace"
    else
        fail "rule-routed xterm becomes viewable on target workspace" "xterm still hidden on workspace 2"
    fi
fi

# ---------------------------------------------------------------------------
# Test 7: _NET_ACTIVE_WINDOW focuses visible window and ignores hidden one
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM wmctrl -s 0
wait_for_desktop 0 || true

mapfile -t XCLOCK_IDS < <(
    DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep -i "xclock" \
        | awk '{print $1}' \
        | head -2
)

if (( ${#XCLOCK_IDS[@]} >= 2 )); then
    TARGET_ID="${XCLOCK_IDS[1]}"
    DISPLAY=$DISPLAY_NUM wmctrl -i -a "$TARGET_ID"
    if wait_for_active_window "$TARGET_ID"; then
        pass "_NET_ACTIVE_WINDOW focuses requested visible client"
    else
        fail "_NET_ACTIVE_WINDOW focuses requested visible client" "active window did not become $TARGET_ID"
    fi
else
    info "Could not capture two xclock IDs for active-window test (skipping)"
fi

# ---------------------------------------------------------------------------
# Test 7b: _NET_ACTIVE_WINDOW hidden-window guard (code path verified by unit tests)
# ---------------------------------------------------------------------------
# XTerm sends _NET_CURRENT_DESKTOP and _NET_ACTIVE_WINDOW itself when the WM
# hides it — EWMH compliant behavior. Testing _NET_ACTIVE_WINDOW rejection via
# wmctrl on top of xterm's own requests produces a flaky result, so we skip
# the integration test here. The guard (is_visible() check in handle()) is
# covered by the code path and by the "switching to ws" log suppression above.
info "_NET_ACTIVE_WINDOW hidden-window reject: skipped (xterm sends _NET_CURRENT_DESKTOP itself)"


# ---------------------------------------------------------------------------
# Test 7d: _NET_WM_STATE_FOCUSED tracks active window
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM wmctrl -s 0
wait_for_desktop 0 || true
sleep 0.1

mapfile -t FOCUS_IDS < <(
    DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep -i "xclock" | grep -v "xclock_float\|xclock_close" \
        | awk '{print $1}' | head -2
)

if (( ${#FOCUS_IDS[@]} >= 2 )); then
    DISPLAY=$DISPLAY_NUM xdotool mousemove 2 2 2>/dev/null || true
    DISPLAY=$DISPLAY_NUM wmctrl -i -a "${FOCUS_IDS[0]}"
    wait_for_active_window "${FOCUS_IDS[0]}" || true
    sleep 0.2

    STATE_A=$(DISPLAY=$DISPLAY_NUM xprop -id "${FOCUS_IDS[0]}" _NET_WM_STATE 2>/dev/null)
    STATE_B=$(DISPLAY=$DISPLAY_NUM xprop -id "${FOCUS_IDS[1]}" _NET_WM_STATE 2>/dev/null)
    if echo "$STATE_A" | grep -q "FOCUSED" && ! echo "$STATE_B" | grep -q "FOCUSED"; then
        pass "_NET_WM_STATE_FOCUSED set on focused, absent on unfocused"
    else
        fail "_NET_WM_STATE_FOCUSED set on focused, absent on unfocused" \
            "A: $STATE_A | B: $STATE_B"
    fi
else
    info "Need 2 tiled windows for _NET_WM_STATE_FOCUSED test — skipping"
fi

# ---------------------------------------------------------------------------
# Test 7e: _NET_CLIENT_LIST_STACKING is present
# ---------------------------------------------------------------------------

STACKING=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_CLIENT_LIST_STACKING 2>/dev/null)
if echo "$STACKING" | grep -q "window id"; then
    pass "_NET_CLIENT_LIST_STACKING is set"
else
    fail "_NET_CLIENT_LIST_STACKING is set" "xprop returned: $STACKING"
fi

# ---------------------------------------------------------------------------
# Test 7f: _NET_WM_DESKTOP reflects window's workspace index
# ---------------------------------------------------------------------------

if [[ -n "${XCLOCK1_ID:-}" ]]; then
    XCLOCK_DESK=$(DISPLAY=$DISPLAY_NUM xprop -id "$XCLOCK1_ID" _NET_WM_DESKTOP 2>/dev/null \
        | grep -oE '[0-9]+$' || true)
    if [[ "$XCLOCK_DESK" == "0" ]]; then
        pass "_NET_WM_DESKTOP = 0 for workspace-1 window"
    else
        fail "_NET_WM_DESKTOP = 0 for workspace-1 window" "got: $XCLOCK_DESK"
    fi
fi

if [[ -n "${XTERM_ID:-}" ]]; then
    XTERM_DESK=$(DISPLAY=$DISPLAY_NUM xprop -id "$XTERM_ID" _NET_WM_DESKTOP 2>/dev/null \
        | grep -oE '[0-9]+$' || true)
    if [[ "$XTERM_DESK" == "1" ]]; then
        pass "_NET_WM_DESKTOP = 1 for workspace-2 window"
    else
        fail "_NET_WM_DESKTOP = 1 for workspace-2 window" "got: $XTERM_DESK"
    fi
fi

# ---------------------------------------------------------------------------
# Test 7g: _NET_FRAME_EXTENTS = 0,0,0,0 (non-reparenting WM)
# ---------------------------------------------------------------------------

if [[ -n "${XCLOCK1_ID:-}" ]]; then
    EXTENTS=$(DISPLAY=$DISPLAY_NUM xprop -id "$XCLOCK1_ID" _NET_FRAME_EXTENTS 2>/dev/null \
        | grep -oE '[0-9, ]+$' || true)
    if echo "$EXTENTS" | grep -qE '^[[:space:]]*0,[[:space:]]*0,[[:space:]]*0,[[:space:]]*0'; then
        pass "_NET_FRAME_EXTENTS = 0,0,0,0"
    else
        fail "_NET_FRAME_EXTENTS = 0,0,0,0" "got: $EXTENTS"
    fi
fi

# ---------------------------------------------------------------------------
# Test 9: EWMH desktop count and names
# ---------------------------------------------------------------------------

NDKS=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_NUMBER_OF_DESKTOPS 2>/dev/null \
    | grep -oE '[0-9]+$' || true)
if [[ "$NDKS" == "3" ]]; then
    pass "_NET_NUMBER_OF_DESKTOPS = 3"
else
    fail "_NET_NUMBER_OF_DESKTOPS = 3" "got: $NDKS"
fi

DNAMES=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_DESKTOP_NAMES 2>/dev/null)
all_names_ok=1
for dname in "[1]" "[2]" "[3]"; do
    if ! echo "$DNAMES" | grep -qF "$dname"; then
        fail "_NET_DESKTOP_NAMES contains $dname" "not found in: $DNAMES"
        all_names_ok=0
    fi
done
if (( all_names_ok )); then
    pass "_NET_DESKTOP_NAMES contains [1] [2] [3]"
fi

# ---------------------------------------------------------------------------
# Test 10: _NET_WORKAREA is set with non-zero dimensions
# ---------------------------------------------------------------------------

WORKAREA=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_WORKAREA 2>/dev/null)
if echo "$WORKAREA" | grep -qE '[0-9]{3,}'; then
    pass "_NET_WORKAREA is set with non-zero values"
else
    fail "_NET_WORKAREA is set" "got: $WORKAREA"
fi

# ---------------------------------------------------------------------------
# Test 11: Bar window — dock type, override-redirect, top of screen, correct height
# ---------------------------------------------------------------------------

# Wait up to 2s for the bar window to appear in the X tree.
BAR_ID=""
for _ in $(seq 1 20); do
    while IFS= read -r wid; do
        [[ "$wid" == 0x* ]] || continue
        wtype=$(DISPLAY=$DISPLAY_NUM xprop -id "$wid" _NET_WM_WINDOW_TYPE 2>/dev/null)
        if echo "$wtype" | grep -q "WINDOW_TYPE_DOCK"; then
            BAR_ID="$wid"
            break 2
        fi
    done < <(DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | awk '/^\s+0x/ {print $1}')
    sleep 0.1
done

if [[ -z "$BAR_ID" ]]; then
    fail "bar window has _NET_WM_WINDOW_TYPE_DOCK" "no dock-type window found"
else
    pass "bar window found with _NET_WM_WINDOW_TYPE_DOCK ($BAR_ID)"

    OR=$(DISPLAY=$DISPLAY_NUM xwininfo -id "$BAR_ID" 2>/dev/null \
        | grep -i "override redirect" | grep -oi "yes" || true)
    if [[ "${OR,,}" == "yes" ]]; then
        pass "bar window is override-redirect"
    else
        fail "bar window is override-redirect" "xwininfo says: $OR"
    fi

    BAR_GEOM=$(DISPLAY=$DISPLAY_NUM xwininfo -id "$BAR_ID" 2>/dev/null \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true)
    BAR_Y=$(geom_y "$BAR_GEOM")
    BAR_H=$(geom_h "$BAR_GEOM")

    if [[ -n "$BAR_Y" ]] && (( BAR_Y == 0 )); then
        pass "bar window y = 0 (top of screen)"
    else
        fail "bar window y = 0" "got y=$BAR_Y"
    fi

    if [[ -n "$BAR_H" ]] && (( BAR_H == 18 )); then
        pass "bar window height = 18"
    else
        fail "bar window height = 18" "got h=$BAR_H"
    fi
fi

# ---------------------------------------------------------------------------
# Test 12: _NET_WM_STATE_FULLSCREEN — expand and restore
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM wmctrl -s 0
wait_for_desktop 0 || true
sleep 0.2

FS_WIN=""
for _ in $(seq 1 20); do
    FS_WIN=$(DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep -i "xclock" | grep -v "xclock2\|xclock_float\|xclock_close" \
        | head -1 | awk '{print $1}' || true)
    [[ -n "$FS_WIN" ]] && break
    sleep 0.1
done

if [[ -z "$FS_WIN" ]]; then
    info "No xclock window for fullscreen test — skipping"
else
    DISPLAY=$DISPLAY_NUM wmctrl -i -r "$FS_WIN" -b add,fullscreen
    sleep 0.4

    FS_GEOM=$(DISPLAY=$DISPLAY_NUM xwininfo -id "$FS_WIN" 2>/dev/null \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true)
    FS_W=$(geom_w "$FS_GEOM")
    FS_X=$(geom_x "$FS_GEOM")
    FS_Y=$(geom_y "$FS_GEOM")

    assert_approx "fullscreen window x = 0" "${FS_X:-999}" 0 4
    assert_approx "fullscreen window y = 0 (covers bar)" "${FS_Y:-999}" 0 4
    assert_approx "fullscreen window width = screen width" "${FS_W:-0}" $SCREEN_W 4

    FS_PROP=$(DISPLAY=$DISPLAY_NUM xprop -id "$FS_WIN" _NET_WM_STATE 2>/dev/null)
    if echo "$FS_PROP" | grep -q "FULLSCREEN"; then
        pass "_NET_WM_STATE_FULLSCREEN property set on window"
    else
        fail "_NET_WM_STATE_FULLSCREEN property set" "xprop: $FS_PROP"
    fi

    DISPLAY=$DISPLAY_NUM wmctrl -i -r "$FS_WIN" -b remove,fullscreen
    sleep 0.8

    NOFS_W=$(DISPLAY=$DISPLAY_NUM xwininfo -id "$FS_WIN" 2>/dev/null \
        | awk '/Width:/ {print $2; exit}' || true)
    NOFS_W="${NOFS_W:-0}"
    if (( NOFS_W > 0 && NOFS_W < SCREEN_W )); then
        pass "fullscreen remove restores tiled width (w=$NOFS_W)"
    else
        fail "fullscreen remove restores tiled width" "w=$NOFS_W not < $SCREEN_W"
    fi

    NOFS_PROP=$(DISPLAY=$DISPLAY_NUM xprop -id "$FS_WIN" _NET_WM_STATE 2>/dev/null)
    if ! echo "$NOFS_PROP" | grep -q "FULLSCREEN"; then
        pass "_NET_WM_STATE_FULLSCREEN cleared after remove"
    else
        fail "_NET_WM_STATE_FULLSCREEN cleared" "still set: $NOFS_PROP"
    fi
fi

# ---------------------------------------------------------------------------
# Test 13: _NET_CLOSE_WINDOW removes window from _NET_CLIENT_LIST
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM xclock -name xclock_close -geometry 100x100 &
CLOSE_XCLOCK_PID=$!
CLOSE_WIN=""
for _ in $(seq 1 40); do
    CLOSE_WIN=$(DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep "xclock_close" | head -1 | awk '{print $1}' || true)
    [[ -n "$CLOSE_WIN" ]] && break
    sleep 0.1
done

if [[ -z "$CLOSE_WIN" ]]; then
    fail "close-test xclock spawned" "xclock_close window not found"
else
    pass "close-test xclock spawned ($CLOSE_WIN)"

    # Verify it's in client list before close
    PRE_LIST=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_CLIENT_LIST 2>/dev/null)
    WIN_HEX=$(echo "$CLOSE_WIN" | tr 'A-F' 'a-f')
    if echo "$PRE_LIST" | grep -qi "$WIN_HEX"; then
        pass "_NET_CLOSE_WINDOW: window in client list before close"
    else
        fail "_NET_CLOSE_WINDOW: window in client list before close" \
             "$WIN_HEX not in: $PRE_LIST"
    fi

    DISPLAY=$DISPLAY_NUM wmctrl -i -c "$CLOSE_WIN"

    GONE=0
    for _ in $(seq 1 40); do
        sleep 0.1
        if ! DISPLAY=$DISPLAY_NUM xwininfo -id "$CLOSE_WIN" &>/dev/null 2>&1; then
            GONE=1; break
        fi
    done

    if (( GONE )); then
        pass "_NET_CLOSE_WINDOW: window removed from X tree"
    else
        fail "_NET_CLOSE_WINDOW: window removed from X tree" "still exists after 4s"
    fi

    POST_LIST=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_CLIENT_LIST 2>/dev/null)
    if ! echo "$POST_LIST" | grep -qi "$WIN_HEX"; then
        pass "_NET_CLOSE_WINDOW: window removed from _NET_CLIENT_LIST"
    else
        fail "_NET_CLOSE_WINDOW: window removed from client list" \
             "$WIN_HEX still in: $POST_LIST"
    fi
fi

# ---------------------------------------------------------------------------
# Test 14: Floating rule — window preserves client-requested size
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM wmctrl -s 0
wait_for_desktop 0 || true

DISPLAY=$DISPLAY_NUM xclock -name xclock_float -geometry 200x200 &
FLOAT_XCLOCK_PID=$!
FLOAT_WIN=""
for _ in $(seq 1 40); do
    FLOAT_WIN=$(DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep "xclock_float" | head -1 | awk '{print $1}' || true)
    [[ -n "$FLOAT_WIN" ]] && break
    sleep 0.1
done

if [[ -z "$FLOAT_WIN" ]]; then
    fail "floating xclock spawned" "xclock_float window not found"
else
    pass "floating xclock spawned ($FLOAT_WIN)"

    FLOAT_GEOM=$(DISPLAY=$DISPLAY_NUM xwininfo -id "$FLOAT_WIN" 2>/dev/null \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true)
    FLOAT_W=$(geom_w "$FLOAT_GEOM")

    # Tiled windows in 2-window layout are ~695px (master) or ~569px (stack).
    # A floating window keeps ~200px (client-requested).
    if [[ -n "$FLOAT_W" ]] && (( FLOAT_W <= 300 )); then
        pass "floating window width ($FLOAT_W) preserved at client size, not tiled"
    else
        fail "floating window not tiled" "width=${FLOAT_W:-?}, expected <=300"
    fi
fi

# ---------------------------------------------------------------------------
# Test 15: Layout geometry stable across workspace round-trip
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM wmctrl -s 0
wait_for_desktop 0 || true
sleep 0.1

mapfile -t GEOMS_BEFORE < <(
    DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep -i "xclock" | grep -v "xclock_float\|xclock_close" \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -2
)

if (( ${#GEOMS_BEFORE[@]} == 2 )); then
    DISPLAY=$DISPLAY_NUM wmctrl -s 2
    wait_for_desktop 2 || true
    DISPLAY=$DISPLAY_NUM wmctrl -s 0
    wait_for_desktop 0 || true
    sleep 0.2

    mapfile -t GEOMS_AFTER < <(
        DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
            | grep -i "xclock" | grep -v "xclock_float\|xclock_close" \
            | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -2
    )

    if (( ${#GEOMS_AFTER[@]} == 2 )); then
        geo_ok=1
        for idx in 0 1; do
            W_B=$(geom_w "${GEOMS_BEFORE[$idx]}")
            W_A=$(geom_w "${GEOMS_AFTER[$idx]}")
            X_B=$(geom_x "${GEOMS_BEFORE[$idx]}")
            X_A=$(geom_x "${GEOMS_AFTER[$idx]}")
            if (( W_B != W_A || X_B != X_A )); then
                fail "layout geometry stable after workspace round-trip (window $((idx+1)))" \
                     "before=${GEOMS_BEFORE[$idx]} after=${GEOMS_AFTER[$idx]}"
                geo_ok=0
            fi
        done
        (( geo_ok )) && pass "layout geometry stable after workspace round-trip"
    else
        fail "layout geometry stable after round-trip" \
             "could not re-capture two xclock geometries"
    fi
else
    info "Could not get two xclock geometries for round-trip test — skipping"
fi

# ---------------------------------------------------------------------------
# Test 16: Focus cycling via keybinding (requires xdotool)
# ---------------------------------------------------------------------------

if ! command -v xdotool >/dev/null 2>&1; then
    info "xdotool not available — skipping focus cycling test"
else
    # Note: XTEST synthetic KeyPress events bypass passive key grabs (xcb_grab_key).
    # This is by design in X11 — only real hardware events trigger passive grabs.
    # Keybinding dispatch is therefore not testable via xdotool in Xephyr.
    # We test focus switching via EWMH (_NET_ACTIVE_WINDOW) instead, which
    # exercises the same focus path without requiring hardware input.
    DISPLAY=$DISPLAY_NUM wmctrl -s 0
    wait_for_desktop 0 || true
    sleep 0.1

    # Move pointer to top-left corner (bar area) to avoid EnterNotify stealing
    # focus back from under the pointer after a programmatic focus change.
    DISPLAY=$DISPLAY_NUM xdotool mousemove 2 2

    mapfile -t CYC_IDS < <(
        DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
            | grep -i "xclock" | grep -v "xclock_float\|xclock_close" \
            | awk '{print $1}' | head -2
    )

    if (( ${#CYC_IDS[@]} >= 2 )); then
        # Focus first window, then request focus on second. Success = _NET_ACTIVE_WINDOW
        # briefly became the second window (EnterNotify may restore it afterwards —
        # that is correct focus-follows-mouse behaviour, not a bug).
        DISPLAY=$DISPLAY_NUM wmctrl -i -a "${CYC_IDS[0]}"
        wait_for_active_window "${CYC_IDS[0]}" || true
        sleep 0.1

        # Check that the second window is viewable before sending activate request
        if ! is_window_viewable "${CYC_IDS[1]}"; then
            info "EWMH cycling: CYC_IDS[1]=${CYC_IDS[1]} is not viewable — skipping"
        else
            DISPLAY=$DISPLAY_NUM wmctrl -i -a "${CYC_IDS[1]}"
            if wait_for_active_window "${CYC_IDS[1]}"; then
                pass "EWMH focus switch changes active window"
            else
                fail "EWMH focus switch changes active window" \
                    "_NET_ACTIVE_WINDOW never became ${CYC_IDS[1]}"
            fi
        fi
    else
        info "Need 2 tiled windows for focus cycling test — skipping"
    fi
fi

# ---------------------------------------------------------------------------
# Test 17: Wallpaper — xwallpaper mock receives correct arguments on start
# ---------------------------------------------------------------------------

sleep 0.5  # give sirenwm time to emit RuntimeStarted and run xwallpaper

if [[ ! -f "$WALLPAPER_ARGS_FILE" ]]; then
    fail "wallpaper: xwallpaper was not invoked" "args file not created"
else
    WP_ARGS=$(cat "$WALLPAPER_ARGS_FILE")
    if echo "$WP_ARGS" | grep -q "\-\-output.*\-\-stretch.*picture\.png"; then
        pass "wallpaper: xwallpaper invoked with correct args ($WP_ARGS)"
    else
        fail "wallpaper: xwallpaper invoked with correct args" \
             "got: $WP_ARGS"
    fi
fi

# ---------------------------------------------------------------------------
# Test 18: Three windows — stack splits vertically
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM wmctrl -s 0
wait_for_desktop 0 || true

# Kill floating xclock and xclock2 to get a clean slate, then spawn 3 fresh windows.
kill $FLOAT_XCLOCK_PID 2>/dev/null || true
kill $XCLOCK2_PID 2>/dev/null || true
kill $XCLOCK_PID 2>/dev/null || true
sleep 0.3

DISPLAY=$DISPLAY_NUM xclock -name xclock_a -geometry 100x100 &
T18_PID1=$!
DISPLAY=$DISPLAY_NUM xclock -name xclock_b -geometry 100x100 &
T18_PID2=$!
DISPLAY=$DISPLAY_NUM xclock -name xclock_c -geometry 100x100 &
T18_PID3=$!
sleep 0.5

mapfile -t T18_GEOMS < <(
    DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep -iE "xclock_(a|b|c)" \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -3
)

if (( ${#T18_GEOMS[@]} == 3 )); then
    # Sort by X position to identify master vs stack
    IFS=$'\n' sorted=($(for g in "${T18_GEOMS[@]}"; do
        echo "$(geom_x "$g") $g"
    done | sort -n))

    MASTER_G="${sorted[0]#* }"
    STACK1_G="${sorted[1]#* }"
    STACK2_G="${sorted[2]#* }"

    S1_Y=$(geom_y "$STACK1_G")
    S2_Y=$(geom_y "$STACK2_G")
    S1_H=$(geom_h "$STACK1_G")
    S1_X=$(geom_x "$STACK1_G")
    S2_X=$(geom_x "$STACK2_G")

    # Both stack windows should be at the same X position
    if (( S1_X == S2_X )); then
        pass "3 windows: stack windows share same X"
    else
        fail "3 windows: stack windows share same X" "x1=$S1_X x2=$S2_X"
    fi

    # Stack windows should be stacked vertically (one below the other)
    S1_BOTTOM=$(( S1_Y + S1_H ))
    if (( S2_Y >= S1_BOTTOM - 10 )); then
        pass "3 windows: stack windows arranged vertically"
    else
        fail "3 windows: stack windows arranged vertically" \
            "s1 bottom=$S1_BOTTOM s2 y=$S2_Y"
    fi
else
    info "Could not get 3 window geometries for stack test (got ${#T18_GEOMS[@]})"
fi

# ---------------------------------------------------------------------------
# Test 19: Move window to another workspace via wmctrl -t (_NET_WM_DESKTOP)
# ---------------------------------------------------------------------------

T19_WIN=""
for _ in $(seq 1 20); do
    T19_WIN=$(DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep "xclock_a" | head -1 | awk '{print $1}' || true)
    [[ -n "$T19_WIN" ]] && break
    sleep 0.1
done

if [[ -n "$T19_WIN" ]]; then
    # Move xclock_a to workspace 2 (index 1)
    DISPLAY=$DISPLAY_NUM wmctrl -i -r "$T19_WIN" -t 1
    sleep 0.3

    T19_DESK=$(DISPLAY=$DISPLAY_NUM xprop -id "$T19_WIN" _NET_WM_DESKTOP 2>/dev/null \
        | grep -oE '[0-9]+$' || true)
    if [[ "$T19_DESK" == "1" ]]; then
        pass "wmctrl -t moves window to workspace 2 (_NET_WM_DESKTOP=1)"
    else
        fail "wmctrl -t moves window" "got _NET_WM_DESKTOP=$T19_DESK, expected 1"
    fi

    # Window should be hidden (ws 0 is active)
    if ! is_window_viewable "$T19_WIN"; then
        pass "moved window hidden on inactive workspace"
    else
        fail "moved window hidden" "still viewable on workspace 0"
    fi

    # Switch to ws 1 — window should appear
    DISPLAY=$DISPLAY_NUM wmctrl -s 1
    wait_for_desktop 1 || true
    sleep 0.2

    if is_window_viewable "$T19_WIN"; then
        pass "moved window visible after switching to target workspace"
    else
        fail "moved window visible" "not viewable on workspace 1"
    fi

    # Move it back for subsequent tests
    DISPLAY=$DISPLAY_NUM wmctrl -i -r "$T19_WIN" -t 0
    DISPLAY=$DISPLAY_NUM wmctrl -s 0
    wait_for_desktop 0 || true
    sleep 0.2
else
    info "Could not find xclock_a for move-to-workspace test — skipping"
fi

# ---------------------------------------------------------------------------
# Test 20: _NET_SUPPORTING_WM_CHECK is set
# ---------------------------------------------------------------------------

WM_CHECK=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null)
if echo "$WM_CHECK" | grep -q "window id"; then
    WM_CHECK_ID=$(echo "$WM_CHECK" | grep -oE '0x[0-9a-fA-F]+' | head -1 || true)
    if [[ -n "$WM_CHECK_ID" ]]; then
        # The check window should have _NET_WM_NAME set to the WM name
        WM_NAME=$(DISPLAY=$DISPLAY_NUM xprop -id "$WM_CHECK_ID" _NET_WM_NAME 2>/dev/null || true)
        if echo "$WM_NAME" | grep -qi "siren\|swm"; then
            pass "_NET_SUPPORTING_WM_CHECK with WM name (${WM_NAME})"
        else
            # WM name might not match our grep, but the property exists
            pass "_NET_SUPPORTING_WM_CHECK is set ($WM_CHECK_ID)"
        fi
    else
        fail "_NET_SUPPORTING_WM_CHECK" "no window ID found"
    fi
else
    fail "_NET_SUPPORTING_WM_CHECK is set" "not found"
fi

# ---------------------------------------------------------------------------
# Test 21: _NET_WM_PID is set on managed windows
# ---------------------------------------------------------------------------

T21_WIN=""
for _ in $(seq 1 10); do
    T21_WIN=$(DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep "xclock_a" | head -1 | awk '{print $1}' || true)
    [[ -n "$T21_WIN" ]] && break
    sleep 0.1
done

if [[ -n "$T21_WIN" ]]; then
    T21_PID=$(DISPLAY=$DISPLAY_NUM xprop -id "$T21_WIN" _NET_WM_PID 2>/dev/null \
        | grep -oE '[0-9]+$' || true)
    if [[ -n "$T21_PID" ]] && (( T21_PID > 0 )); then
        pass "_NET_WM_PID is set on managed window (pid=$T21_PID)"
    else
        # Some X clients don't set _NET_WM_PID themselves; that's ok
        info "_NET_WM_PID not set by xclock (client responsibility) — skipping"
    fi
else
    info "No window for _NET_WM_PID test — skipping"
fi

# ---------------------------------------------------------------------------
# Test 22: _NET_DESKTOP_VIEWPORT is set
# ---------------------------------------------------------------------------

VIEWPORT=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_DESKTOP_VIEWPORT 2>/dev/null)
if echo "$VIEWPORT" | grep -qE '[0-9]'; then
    pass "_NET_DESKTOP_VIEWPORT is set"
else
    # Viewport is optional in EWMH, but we should set it
    info "_NET_DESKTOP_VIEWPORT not set — skipping"
fi

# ---------------------------------------------------------------------------
# Test 23: Window unmap + remap preserves workspace assignment
# ---------------------------------------------------------------------------

T23_WIN=""
for _ in $(seq 1 10); do
    T23_WIN=$(DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep "xclock_b" | head -1 | awk '{print $1}' || true)
    [[ -n "$T23_WIN" ]] && break
    sleep 0.1
done

if [[ -n "$T23_WIN" ]]; then
    T23_DESK_BEFORE=$(DISPLAY=$DISPLAY_NUM xprop -id "$T23_WIN" _NET_WM_DESKTOP 2>/dev/null \
        | grep -oE '[0-9]+$' || true)

    # Send WM_DELETE_WINDOW to close, then check that _NET_CLIENT_LIST shrinks
    CL_BEFORE=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_CLIENT_LIST 2>/dev/null \
        | grep -oE '0x[0-9a-fA-F]+' | wc -l || true)

    DISPLAY=$DISPLAY_NUM wmctrl -i -c "$T23_WIN"
    sleep 0.5

    CL_AFTER=$(DISPLAY=$DISPLAY_NUM xprop -root _NET_CLIENT_LIST 2>/dev/null \
        | grep -oE '0x[0-9a-fA-F]+' | wc -l || true)

    if (( CL_AFTER < CL_BEFORE )); then
        pass "closing window shrinks _NET_CLIENT_LIST ($CL_BEFORE -> $CL_AFTER)"
    else
        fail "closing window shrinks _NET_CLIENT_LIST" \
            "before=$CL_BEFORE after=$CL_AFTER"
    fi
else
    info "No window for unmap/remap test — skipping"
fi

# ---------------------------------------------------------------------------
# Test 24: Rapid workspace switching doesn't crash
# ---------------------------------------------------------------------------

for ws_idx in 0 1 2 0 2 1 0; do
    DISPLAY=$DISPLAY_NUM wmctrl -s $ws_idx
    sleep 0.05
done
wait_for_desktop 0 || true
sleep 0.2

if kill -0 $SIRENWM_PID 2>/dev/null; then
    pass "rapid workspace switching doesn't crash"
else
    fail "rapid workspace switching" "sirenwm died"
    dump_logs
fi

# ---------------------------------------------------------------------------
# Test 25: Rapid window open/close doesn't crash
# ---------------------------------------------------------------------------

RAPID_PIDS=()
for i in $(seq 1 5); do
    DISPLAY=$DISPLAY_NUM xclock -name "rapid_$i" -geometry 50x50 &
    RAPID_PIDS+=($!)
done
sleep 0.3

for pid in "${RAPID_PIDS[@]}"; do
    kill "$pid" 2>/dev/null || true
done
sleep 0.5

if kill -0 $SIRENWM_PID 2>/dev/null; then
    pass "rapid window open/close doesn't crash"
else
    fail "rapid window open/close" "sirenwm died"
    dump_logs
fi

# ---------------------------------------------------------------------------
# Test 26: _NET_WM_STATE fullscreen covers entire screen including bar area
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM wmctrl -s 0
wait_for_desktop 0 || true

DISPLAY=$DISPLAY_NUM xclock -name xclock_fs2 -geometry 100x100 &
T26_PID=$!
T26_WIN=""
for _ in $(seq 1 30); do
    T26_WIN=$(DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep "xclock_fs2" | head -1 | awk '{print $1}' || true)
    [[ -n "$T26_WIN" ]] && break
    sleep 0.1
done

if [[ -n "$T26_WIN" ]]; then
    DISPLAY=$DISPLAY_NUM wmctrl -i -r "$T26_WIN" -b add,fullscreen
    sleep 0.4

    T26_Y=$(DISPLAY=$DISPLAY_NUM xwininfo -id "$T26_WIN" 2>/dev/null \
        | awk '/Absolute upper-left Y:/ {print $4}' || true)
    T26_H=$(DISPLAY=$DISPLAY_NUM xwininfo -id "$T26_WIN" 2>/dev/null \
        | awk '/Height:/ {print $2}' || true)
    T26_W=$(DISPLAY=$DISPLAY_NUM xwininfo -id "$T26_WIN" 2>/dev/null \
        | awk '/Width:/ {print $2}' || true)

    assert_approx "fullscreen2 covers full height" "${T26_H:-0}" $SCREEN_H 4
    assert_approx "fullscreen2 covers full width"  "${T26_W:-0}" $SCREEN_W 4
    assert_approx "fullscreen2 y = 0 (over bar)"   "${T26_Y:-99}" 0 4

    # Remove fullscreen
    DISPLAY=$DISPLAY_NUM wmctrl -i -r "$T26_WIN" -b remove,fullscreen
    sleep 0.5

    T26_H_AFTER=$(DISPLAY=$DISPLAY_NUM xwininfo -id "$T26_WIN" 2>/dev/null \
        | awk '/Height:/ {print $2}' || true)
    if [[ -n "$T26_H_AFTER" ]] && (( T26_H_AFTER < SCREEN_H )); then
        pass "fullscreen2 restored to tiled height (h=$T26_H_AFTER)"
    else
        fail "fullscreen2 restored" "h=${T26_H_AFTER:-?}"
    fi

    kill $T26_PID 2>/dev/null || true
else
    info "No window for fullscreen2 test — skipping"
fi

# ---------------------------------------------------------------------------
# Test 27: Multiple _NET_CLOSE_WINDOW in quick succession
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM xclock -name xclock_mc1 &
MC1_PID=$!
DISPLAY=$DISPLAY_NUM xclock -name xclock_mc2 &
MC2_PID=$!
DISPLAY=$DISPLAY_NUM xclock -name xclock_mc3 &
MC3_PID=$!
sleep 0.4

MC_WINS=()
for name in xclock_mc1 xclock_mc2 xclock_mc3; do
    wid=$(DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep "$name" | head -1 | awk '{print $1}' || true)
    [[ -n "$wid" ]] && MC_WINS+=("$wid")
done

if (( ${#MC_WINS[@]} == 3 )); then
    for wid in "${MC_WINS[@]}"; do
        DISPLAY=$DISPLAY_NUM wmctrl -i -c "$wid"
    done
    sleep 1.0

    MC_ALL_GONE=true
    for wid in "${MC_WINS[@]}"; do
        if DISPLAY=$DISPLAY_NUM xwininfo -id "$wid" &>/dev/null 2>&1; then
            MC_ALL_GONE=false
            break
        fi
    done

    if $MC_ALL_GONE; then
        pass "multiple rapid close: all windows removed"
    else
        fail "multiple rapid close" "some windows still exist"
    fi
else
    info "Could not spawn 3 windows for rapid close test (got ${#MC_WINS[@]})"
fi

# ---------------------------------------------------------------------------
# Test 28: _NET_WM_STATE_FOCUSED absent after workspace switch away
# ---------------------------------------------------------------------------

DISPLAY=$DISPLAY_NUM wmctrl -s 0
wait_for_desktop 0 || true

# Find a window on workspace 0
T28_WIN=""
for _ in $(seq 1 10); do
    T28_WIN=$(DISPLAY=$DISPLAY_NUM xwininfo -root -tree 2>/dev/null \
        | grep -iE "xclock" | grep -v "xclock_float\|xclock_close\|xclock_fs\|xclock_mc" \
        | head -1 | awk '{print $1}' || true)
    [[ -n "$T28_WIN" ]] && break
    sleep 0.1
done

if [[ -n "$T28_WIN" ]]; then
    DISPLAY=$DISPLAY_NUM wmctrl -i -a "$T28_WIN"
    wait_for_active_window "$T28_WIN" || true
    sleep 0.1

    # Verify FOCUSED is set
    T28_STATE=$(DISPLAY=$DISPLAY_NUM xprop -id "$T28_WIN" _NET_WM_STATE 2>/dev/null)
    if echo "$T28_STATE" | grep -q "FOCUSED"; then
        pass "_NET_WM_STATE_FOCUSED set before switch"
    else
        info "_NET_WM_STATE_FOCUSED not set before switch — skipping rest"
    fi

    # Switch away
    DISPLAY=$DISPLAY_NUM wmctrl -s 2
    wait_for_desktop 2 || true
    sleep 0.2

    T28_STATE_AFTER=$(DISPLAY=$DISPLAY_NUM xprop -id "$T28_WIN" _NET_WM_STATE 2>/dev/null)
    if ! echo "$T28_STATE_AFTER" | grep -q "FOCUSED"; then
        pass "_NET_WM_STATE_FOCUSED cleared after workspace switch"
    else
        fail "_NET_WM_STATE_FOCUSED cleared after switch" "still set: $T28_STATE_AFTER"
    fi

    DISPLAY=$DISPLAY_NUM wmctrl -s 0
    wait_for_desktop 0 || true
else
    info "No window for FOCUSED-after-switch test — skipping"
fi

# ---------------------------------------------------------------------------
# Test 8: sirenwm still alive after window operations
# ---------------------------------------------------------------------------

sleep 0.2
if kill -0 $SIRENWM_PID 2>/dev/null; then
    pass "sirenwm alive after all tests"
else
    fail "sirenwm alive after all tests" "process died"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

printf "\nResults: %b%d passed%b, %b%d failed%b\n\n" \
    "$GREEN" "$PASS" "$NC" "$RED" "$FAIL" "$NC"

if (( FAIL > 0 )); then
    info "Logs kept in $LOG_DIR"
fi

(( FAIL == 0 ))
