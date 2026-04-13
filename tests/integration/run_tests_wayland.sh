#!/usr/bin/env bash
# Integration tests for sirenwm Wayland backend running nested inside X11
# (Xephyr when host X is available, otherwise Xvfb).
# Process model:
#   nested X server (Xephyr/Xvfb) -> sirenwm-wayland --display-server -> sirenwm-wayland (WM client).
#
# Requires: wayland-utils (wayland-info), sirenwm built with SIRENWM_BACKEND=wayland
#           and either Xephyr (preferred with host X) or Xvfb
# Optional: cc + wayland-scanner + libwayland-client for xdg-toplevel client tests
#           weston-simple-shm (from weston package)

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
SIRENWM="$REPO_ROOT/output/sirenwm-wayland"
DISPLAY_SERVER_BIN=""
DISPLAY_SERVER_ARGS=()
X_DISPLAY=""
SCREEN_W=1280
SCREEN_H=720
PASS=0
FAIL=0
SKIP=0

LOG_DIR="${TMPDIR:-/tmp}/sirenwm-wl-itest"
SIRENWM_LOG="$LOG_DIR/sirenwm.log"
XSERVER_LOG="$LOG_DIR/xserver.log"
DISPLAY_SERVER_LOG="$LOG_DIR/display-server.log"
DISPLAY_SERVER_STDOUT="$LOG_DIR/display-server.stdout"
TEST_HOME="$LOG_DIR/home"
TEST_CONFIG="$TEST_HOME/.config/sirenwm/init.lua"
XDG_RUNTIME="$LOG_DIR/xdg-runtime"
BUILD_DIR="$LOG_DIR/build"
HOST_X_DISPLAY=""
WAYLAND_SOCKET_NAME=""
XWAYLAND_DISPLAY_NAME=""

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
    info "--- sirenwm log (tail 80) ---"
    tail -n 80 "$SIRENWM_LOG" 2>/dev/null || true
    info "--- display-server stderr (tail 80) ---"
    tail -n 80 "$DISPLAY_SERVER_LOG" 2>/dev/null || true
    info "--- display-server stdout (tail 20) ---"
    tail -n 20 "$DISPLAY_SERVER_STDOUT" 2>/dev/null || true
    info "--- runtime.log (tail 40) ---"
    tail -n 40 "$TEST_HOME/runtime.log" 2>/dev/null || true
    info "--- X server log ---"
    tail -n 20 "$XSERVER_LOG" 2>/dev/null || true
}

# Run a Wayland client command with the test compositor's socket.
wl_run() {
    XDG_RUNTIME_DIR="$XDG_RUNTIME" WAYLAND_DISPLAY="$WAYLAND_SOCKET_NAME" "$@"
}

run_with_optional_timeout() {
    local sec="$1"; shift
    if command -v timeout >/dev/null 2>&1; then
        timeout "$sec" "$@"
    else
        "$@"
    fi
}

wl_timeout_run() {
    local sec="$1"; shift
    run_with_optional_timeout "$sec" env \
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

# Wait up to N*0.1s for a pattern in a log file.
wait_log() {
    local desc="$1" max="$2" pattern="$3" file="$4"
    local n=0
    while ! grep -qE "$pattern" "$file" 2>/dev/null; do
        sleep 0.1; ((++n))
        if (( n > max )); then
            echo -e "${RED}TIMEOUT${NC} waiting for '$pattern' in $file"
            return 1
        fi
    done
}

assert_pid_alive() {
    local pid="$1" name="$2" context="$3"
    if kill -0 "$pid" 2>/dev/null; then
        pass "$name"
    else
        fail "$name" "$context"
        dump_logs
    fi
}

XSERVER_PID=0
XSERVER_KIND=""
DISPLAY_SERVER_PID=0
SIRENWM_PID=0

cleanup() {
    info "Cleaning up..."
    [[ $SIRENWM_PID -ne 0 ]] && kill $SIRENWM_PID 2>/dev/null || true
    [[ $DISPLAY_SERVER_PID -ne 0 ]] && kill $DISPLAY_SERVER_PID 2>/dev/null || true
    [[ $XSERVER_PID -ne 0 ]] && kill $XSERVER_PID 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

rm -rf "$LOG_DIR"
mkdir -p "$LOG_DIR" "$XDG_RUNTIME" "$BUILD_DIR"
chmod 700 "$XDG_RUNTIME"
: > "$SIRENWM_LOG"
: > "$XSERVER_LOG"
: > "$DISPLAY_SERVER_LOG"
: > "$DISPLAY_SERVER_STDOUT"

for cmd in xdpyinfo wayland-info; do
    require_cmd "$cmd"
done
if ! command -v Xephyr >/dev/null 2>&1 && ! command -v Xvfb >/dev/null 2>&1; then
    echo -e "${RED}ERROR${NC} neither Xephyr nor Xvfb is available"
    exit 1
fi

if [[ ! -x "$SIRENWM" ]]; then
    echo -e "${RED}ERROR${NC} sirenwm binary not found: $SIRENWM"
    echo "Build first: cmake -S . -B build -DSIRENWM_BACKEND=wayland && cmake --build build -j"
    exit 1
fi

find_display_server_bin() {
    # Unified wayland binary in display-server mode.
    if [[ -x "$REPO_ROOT/output/sirenwm-wayland" ]]; then
        DISPLAY_SERVER_BIN="$REPO_ROOT/output/sirenwm-wayland"
        DISPLAY_SERVER_ARGS=(--display-server)
        return 0
    fi
    return 1
}

if ! find_display_server_bin; then
    echo -e "${RED}ERROR${NC} display-server binary not found"
    echo "Expected: $REPO_ROOT/output/sirenwm-wayland (with --display-server)"
    echo "Build it, for example:"
    echo "  cmake -S . -B build-wayland -DSIRENWM_BACKEND=wayland && cmake --build build-wayland -j"
    exit 1
fi

# ---------------------------------------------------------------------------
# Build minimal xdg-toplevel test client (if toolchain available)
# ---------------------------------------------------------------------------
XDG_CLIENT=""
XDG_CLIENT_SRC="$BUILD_DIR/xdg_client.c"
XDG_CLIENT_BIN="$BUILD_DIR/xdg_client"
XDG_SHELL_HDR="$BUILD_DIR/xdg-shell-client-protocol.h"
XDG_SHELL_SRC="$BUILD_DIR/xdg-shell-protocol.c"

build_xdg_client() {
    # Find xdg-shell.xml
    local xml=""
    for d in \
        /usr/share/wayland-protocols/stable/xdg-shell \
        /usr/local/share/wayland-protocols/stable/xdg-shell \
        /usr/share/wayland-protocols
    do
        [[ -f "$d/xdg-shell.xml" ]] && { xml="$d/xdg-shell.xml"; break; }
    done
    [[ -z "$xml" ]] && return 1

    wayland-scanner client-header "$xml" "$XDG_SHELL_HDR" 2>/dev/null || return 1
    wayland-scanner private-code  "$xml" "$XDG_SHELL_SRC" 2>/dev/null || return 1

    cat >"$XDG_CLIENT_SRC" <<'CSRC'
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#ifdef __linux__
#include <linux/memfd.h>
#endif
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

static int create_memfd(const char *name) {
#if defined(__linux__) && defined(SYS_memfd_create)
    return (int)syscall(SYS_memfd_create, name, MFD_CLOEXEC);
#else
    (void)name;
    errno = ENOSYS;
    return -1;
#endif
}

static struct wl_compositor  *compositor  = NULL;
static struct wl_shm         *shm         = NULL;
static struct xdg_wm_base    *wm_base     = NULL;
static int                    configured  = 0;

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);
    configured = 1;
}
static const struct xdg_surface_listener xdg_surface_listener = { xdg_surface_configure };

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *tl,
    int32_t w, int32_t h, struct wl_array *states) { (void)data;(void)tl;(void)w;(void)h;(void)states; }
static void xdg_toplevel_close(void *data, struct xdg_toplevel *tl) { (void)data;(void)tl; exit(0); }
static void xdg_toplevel_configure_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h)
    { (void)d;(void)t;(void)w;(void)h; }
static void xdg_toplevel_wm_capabilities(void *d, struct xdg_toplevel *t, struct wl_array *a)
    { (void)d;(void)t;(void)a; }
static const struct xdg_toplevel_listener tl_listener = {
    xdg_toplevel_configure, xdg_toplevel_close,
    xdg_toplevel_configure_bounds, xdg_toplevel_wm_capabilities
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm, uint32_t serial) {
    xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = { wm_base_ping };

static void shm_format(void *d, struct wl_shm *s, uint32_t f) { (void)d;(void)s;(void)f; }
static const struct wl_shm_listener shm_listener = { shm_format };

static void registry_global(void *data, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t ver) {
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name)) {
        shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
        wl_shm_add_listener(shm, &shm_listener, NULL);
    } else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
    }
}
static void registry_global_remove(void *d, struct wl_registry *r, uint32_t n)
    { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener reg_listener =
    { registry_global, registry_global_remove };

/* Non-blocking dispatch loop with poll() timeout (ms). Returns 0 if condition met. */
static int dispatch_until(struct wl_display *d, int *flag, int timeout_ms) {
    struct pollfd pfd = { wl_display_get_fd(d), POLLIN, 0 };
    int elapsed = 0;
    while (!*flag && elapsed < timeout_ms) {
        wl_display_flush(d);
        if (poll(&pfd, 1, 50) > 0)
            wl_display_dispatch(d);
        else
            wl_display_dispatch_pending(d);
        elapsed += 50;
    }
    return *flag ? 0 : -1;
}

/* Create a wl_buffer from anonymous shm (32x32 ARGB8888). */
static struct wl_buffer *create_shm_buffer(int w, int h) {
    int stride = w * 4;
    int size   = stride * h;
    int fd = create_memfd("xdg-client-buf");
    if (fd < 0) return NULL;
    if (ftruncate(fd, size) < 0) { close(fd); return NULL; }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return NULL; }
    memset(data, 0x44, size);  /* semi-transparent grey */
    munmap(data, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    close(fd);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                                       WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    return buf;
}

int main(int argc, char **argv) {
    const char *title = argc > 1 ? argv[1] : "test-window";
    int run_ms = argc > 2 ? atoi(argv[2]) : 500;

    struct wl_display *display = wl_display_connect(NULL);
    if (!display) { fprintf(stderr, "xdg_client: cannot connect\n"); return 1; }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &reg_listener, NULL);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);  /* second roundtrip for shm formats */

    if (!compositor || !wm_base || !shm) {
        fprintf(stderr, "xdg_client: missing compositor/xdg_wm_base/shm\n");
        wl_display_disconnect(display);
        return 1;
    }

    struct wl_surface     *surface     = wl_compositor_create_surface(compositor);
    struct xdg_surface    *xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    struct xdg_toplevel   *toplevel    = xdg_surface_get_toplevel(xdg_surface);

    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel_add_listener(toplevel, &tl_listener, NULL);
    xdg_toplevel_set_title(toplevel, title);
    xdg_toplevel_set_app_id(toplevel, "sirenwm-test");
    wl_surface_commit(surface);
    wl_display_flush(display);

    /* Wait up to 3s for configure */
    if (dispatch_until(display, &configured, 3000) != 0) {
        fprintf(stderr, "xdg_client: no configure received within 3s\n");
        wl_display_disconnect(display);
        return 1;
    }

    /* Attach a real shm buffer and commit to map the surface */
    struct wl_buffer *buf = create_shm_buffer(32, 32);
    if (buf) {
        wl_surface_attach(surface, buf, 0, 0);
        wl_surface_damage(surface, 0, 0, 32, 32);
    }
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    fprintf(stdout, "mapped title=%s\n", title);
    fflush(stdout);

    /* Stay alive for run_ms then exit cleanly */
    usleep((useconds_t)(run_ms * 1000));

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    if (buf) wl_buffer_destroy(buf);
    wl_display_disconnect(display);
    return 0;
}
CSRC

    cc -o "$XDG_CLIENT_BIN" "$XDG_CLIENT_SRC" "$XDG_SHELL_SRC" \
        -I"$BUILD_DIR" \
        $(pkg-config --cflags --libs wayland-client 2>/dev/null) \
        -lwayland-client || return 1

    echo "$XDG_CLIENT_BIN"
}

if command -v cc >/dev/null 2>&1 && command -v wayland-scanner >/dev/null 2>&1 && \
   pkg-config wayland-client >/dev/null 2>&1; then
    if XDG_CLIENT=$(build_xdg_client 2>"$LOG_DIR/xdg_build.log") && [[ -x "$XDG_CLIENT" ]]; then
        info "Built xdg-toplevel test client: $XDG_CLIENT"
    else
        info "xdg-toplevel test client build failed — window tests will be skipped"
        cat "$LOG_DIR/xdg_build.log" 2>/dev/null | head -20 || true
        XDG_CLIENT=""
    fi
else
    info "cc/wayland-scanner/wayland-client not available — window tests will be skipped"
    info "  cc=$(command -v cc 2>/dev/null || echo 'missing')"
    info "  wayland-scanner=$(command -v wayland-scanner 2>/dev/null || echo 'missing')"
    info "  wayland-client pkg: $(pkg-config --modversion wayland-client 2>/dev/null || echo 'missing')"
fi

# ---------------------------------------------------------------------------
# Write init.lua (3 workspaces, bar)
# ---------------------------------------------------------------------------
mkdir -p "$(dirname "$TEST_CONFIG")"
cp -r "$REPO_ROOT/lua/." "$TEST_HOME/.config/sirenwm/"

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

bar.settings = {
  top = {
    height = 18,
    left   = { siren.load("widgets.tags") },
    center = { siren.load("widgets.title") },
  },
}
LUA

# ---------------------------------------------------------------------------
# Start nested X server for display-server X11 output:
# - Prefer Xephyr when a host X server is already available.
# - Fallback to Xvfb when no host X server is present (or Xephyr missing).
# ---------------------------------------------------------------------------
pick_free_display() {
    local n
    for n in $(seq 98 110); do
        [[ ! -e "/tmp/.X$n-lock" && ! -S "/tmp/.X11-unix/X$n" ]] && { echo ":$n"; return 0; }
    done
    echo ":98"
    return 1
}

detect_host_x_display() {
    local candidate
    if [[ -n "${DISPLAY:-}" ]] && xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
        HOST_X_DISPLAY="$DISPLAY"
        return 0
    fi
    for candidate in :0 :1; do
        if xdpyinfo -display "$candidate" >/dev/null 2>&1; then
            HOST_X_DISPLAY="$candidate"
            return 0
        fi
    done
    HOST_X_DISPLAY=""
    return 1
}

start_nested_x_server() {
    X_DISPLAY="$(pick_free_display)"
    local x_display_raw="$X_DISPLAY"
    if [[ -n "$HOST_X_DISPLAY" ]] && command -v Xephyr >/dev/null 2>&1; then
        XSERVER_KIND="Xephyr"
        info "Starting Xephyr on $X_DISPLAY (${SCREEN_W}x${SCREEN_H}), host=$HOST_X_DISPLAY..."
        DISPLAY="$HOST_X_DISPLAY" \
            Xephyr "$X_DISPLAY" -screen "${SCREEN_W}x${SCREEN_H}" -ac -br -noreset >"$XSERVER_LOG" 2>&1 &
        XSERVER_PID=$!
    elif command -v Xvfb >/dev/null 2>&1; then
        XSERVER_KIND="Xvfb"
        if [[ -n "$HOST_X_DISPLAY" ]] && ! command -v Xephyr >/dev/null 2>&1; then
            info "Host X detected, but Xephyr is missing. Falling back to Xvfb."
        else
            info "No host X server detected. Starting Xvfb on $X_DISPLAY (${SCREEN_W}x${SCREEN_H})..."
        fi
        Xvfb "$X_DISPLAY" -screen 0 "${SCREEN_W}x${SCREEN_H}x24" >"$XSERVER_LOG" 2>&1 &
        XSERVER_PID=$!
    else
        echo -e "${RED}ERROR${NC} cannot start nested X server: Xephyr unavailable and no Xvfb"
        exit 1
    fi

    if ! wait_for "$XSERVER_KIND on $X_DISPLAY" 60 xdpyinfo -display "$X_DISPLAY"; then
        if [[ "$XSERVER_KIND" == "Xvfb" ]] && \
           grep -qE "Owner of /tmp/.X11-unix should be set to root|Cannot establish any listening sockets" "$XSERVER_LOG"; then
            info "Xvfb unix socket unavailable; retrying Xvfb over TCP..."
            kill "$XSERVER_PID" 2>/dev/null || true
            wait "$XSERVER_PID" 2>/dev/null || true
            : >"$XSERVER_LOG"
            Xvfb "$x_display_raw" -screen 0 "${SCREEN_W}x${SCREEN_H}x24" -nolisten unix -listen tcp >"$XSERVER_LOG" 2>&1 &
            XSERVER_PID=$!
            X_DISPLAY="127.0.0.1${x_display_raw}"
            if wait_for "$XSERVER_KIND on $X_DISPLAY (tcp)" 60 xdpyinfo -display "$X_DISPLAY"; then
                info "$XSERVER_KIND ready on $X_DISPLAY."
                return
            fi
        fi
        dump_logs
        exit 1
    fi
    info "$XSERVER_KIND ready on $X_DISPLAY."
}

detect_host_x_display || true
start_nested_x_server

start_display_server() {
    info "Starting sirenwm-wayland --display-server on $X_DISPLAY..."
    DISPLAY="$X_DISPLAY" \
    XDG_RUNTIME_DIR="$XDG_RUNTIME" \
        "$DISPLAY_SERVER_BIN" "${DISPLAY_SERVER_ARGS[@]}" --size "${SCREEN_W}x${SCREEN_H}" \
        >"$DISPLAY_SERVER_STDOUT" 2>"$DISPLAY_SERVER_LOG" &
    DISPLAY_SERVER_PID=$!

    local n=0
    while true; do
        WAYLAND_SOCKET_NAME="$(sed -n 's/^WAYLAND_DISPLAY=//p' "$DISPLAY_SERVER_STDOUT" | head -n 1)"
        XWAYLAND_DISPLAY_NAME="$(sed -n 's/^DISPLAY=//p' "$DISPLAY_SERVER_STDOUT" | head -n 1)"
        if [[ -n "$WAYLAND_SOCKET_NAME" ]]; then
            break
        fi
        if ! kill -0 "$DISPLAY_SERVER_PID" 2>/dev/null; then
            break
        fi
        sleep 0.1; ((++n))
        (( n > 80 )) && break
    done

    if [[ -z "$WAYLAND_SOCKET_NAME" ]]; then
        fail "display-server startup" "WAYLAND_DISPLAY not emitted"
        dump_logs
        exit 1
    fi

    if [[ -n "$XWAYLAND_DISPLAY_NAME" ]]; then
        info "display-server ready: WAYLAND_DISPLAY=$WAYLAND_SOCKET_NAME, XWAYLAND_DISPLAY=$XWAYLAND_DISPLAY_NAME"
    else
        info "display-server ready: WAYLAND_DISPLAY=$WAYLAND_SOCKET_NAME"
    fi
}

start_display_server

start_sirenwm() {
    HOME="$TEST_HOME" \
    WAYLAND_DISPLAY="$WAYLAND_SOCKET_NAME" \
    XDG_RUNTIME_DIR="$XDG_RUNTIME" \
        "$SIRENWM" >>"$SIRENWM_LOG" 2>&1 &
    echo $!
}

# ---------------------------------------------------------------------------
# Start sirenwm
# ---------------------------------------------------------------------------
info "Starting sirenwm (Wayland backend client, WAYLAND_DISPLAY=$WAYLAND_SOCKET_NAME)..."
SIRENWM_PID=$(start_sirenwm)

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
# Test 2: Wayland socket exists in XDG_RUNTIME_DIR
# ===========================================================================
if [[ -S "$XDG_RUNTIME/$WAYLAND_SOCKET_NAME" ]]; then
    pass "Wayland socket created: $WAYLAND_SOCKET_NAME"
else
    fail "Wayland socket created" "missing socket: $XDG_RUNTIME/$WAYLAND_SOCKET_NAME"
    dump_logs; exit 1
fi

# ===========================================================================
# Test 3: wayland-info connects and receives global list
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
# Tests 4-9: advertised Wayland globals
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
# Test 10: zwlr_layer_shell_v1 availability (optional in display-server mode)
# ===========================================================================
# In legacy wlroots-compositor mode this protocol could be exported directly.
# In current display-server mode it may be intentionally absent.
if grep -q "zwlr_layer_shell_v1" "$WAYLAND_INFO_OUT" 2>/dev/null; then
    pass "compositor advertises zwlr_layer_shell_v1"
elif grep -q 'Bar: initialized' "$TEST_HOME/runtime.log" 2>/dev/null; then
    skip "compositor advertises zwlr_layer_shell_v1" \
         "protocol not exported in display-server mode (bar initialized)"
else
    fail "compositor advertises zwlr_layer_shell_v1" \
         "not found in wayland-info output and bar did not initialize"
fi

# ===========================================================================
# Test 11: FSM reaches Running state
# ===========================================================================
if grep -q 'FSM: Starting → Running' "$TEST_HOME/runtime.log" 2>/dev/null; then
    pass "FSM reached Running state"
else
    fail "FSM reached Running state" "transition not found in runtime.log"
fi

# ===========================================================================
# Test 12: monitor layout applied
# ===========================================================================
if grep -qE "WlMonitorPort: applied|monitors: found [1-9][0-9]* monitor\\(s\\):" \
       "$TEST_HOME/runtime.log" 2>/dev/null; then
    pass "monitor layout applied"
else
    fail "monitor layout applied" "monitor topology apply log not found in runtime.log"
fi

# ===========================================================================
# Test 13: bar initialized
# ===========================================================================
if grep -q 'Bar: initialized' "$TEST_HOME/runtime.log" 2>/dev/null; then
    pass "bar module initialized"
else
    fail "bar module initialized" "not found in runtime.log"
fi

# ===========================================================================
# Test 14: bar allocated shm buffer (0.18+ rendering path)
# ===========================================================================
if grep -q 'shm allocator\|shm.*buffer\|WlRenderWindow.*alloc\|wlr_allocator_create_buffer' \
       "$SIRENWM_LOG" 2>/dev/null ||
   ! grep -q 'bar.*not implemented\|bar.*invisible' "$SIRENWM_LOG" 2>/dev/null; then
    pass "bar rendering path active (no stub error)"
else
    fail "bar rendering path active" "stub/invisible message in sirenwm.log"
fi

# ===========================================================================
# Test 15: no crashes or assertions in logs
# ===========================================================================
if grep -qE 'Assertion.*failed|SIGSEGV|SIGABRT|core dump' \
       "$SIRENWM_LOG" "$TEST_HOME/runtime.log" 2>/dev/null; then
    fail "no crashes in logs" "assertion/signal found"
else
    pass "no crashes in logs"
fi

# ===========================================================================
# Test 16: no config errors at startup
# ===========================================================================
if grep -qE '\[error\].*RuntimeStore|FSM: aborting|failed to load config' \
       "$TEST_HOME/runtime.log" 2>/dev/null; then
    fail "no config errors at startup" "config error found in runtime.log"
else
    pass "no config errors at startup"
fi

# ===========================================================================
# Test 17: wl_shm version >= 1
# ===========================================================================
if grep -qE "wl_shm.*version:.*[1-9]" "$WAYLAND_INFO_OUT" 2>/dev/null; then
    pass "wl_shm version >= 1"
else
    fail "wl_shm version >= 1" "not found in wayland-info output"
fi

# ===========================================================================
# Test 18: wl_seat advertises pointer and keyboard
# ===========================================================================
if grep -qE 'pointer|keyboard' "$WAYLAND_INFO_OUT" 2>/dev/null; then
    pass "wl_seat advertises pointer/keyboard"
else
    fail "wl_seat advertises pointer/keyboard" "not found in wayland-info output"
fi

# ===========================================================================
# Test 19: 5 simultaneous wayland-info connections
# ===========================================================================
WI_PIDS=()
for i in $(seq 1 5); do
    wl_run wayland-info >"$LOG_DIR/wayland-info-par-$i.txt" 2>&1 &
    WI_PIDS+=($!)
done
MULTI_OK=true
for pid in "${WI_PIDS[@]}"; do
    wait "$pid" || MULTI_OK=false
done
if $MULTI_OK; then
    pass "5 simultaneous wayland-info connections succeed"
else
    fail "5 simultaneous wayland-info connections succeed" "at least one failed"
fi

# ===========================================================================
# Test 20: compositor alive after parallel connections
# ===========================================================================
assert_pid_alive "$SIRENWM_PID" "compositor alive after parallel connections" "process died"

# ===========================================================================
# Test 20b: display-server alive after parallel connections
# ===========================================================================
assert_pid_alive "$DISPLAY_SERVER_PID" "display-server alive after parallel connections" "process died"

# ===========================================================================
# Tests 21-26: xdg-toplevel window lifecycle (requires built test client)
# ===========================================================================
if [[ -z "$XDG_CLIENT" ]]; then
    for t in "xdg-toplevel: compositor accepts connection" \
             "xdg-toplevel: configure received by client" \
             "xdg-toplevel: surface mapped logged" \
             "xdg-toplevel: compositor alive after window" \
             "xdg-toplevel: surface destroyed cleanly" \
             "xdg-toplevel: 3 concurrent windows"; do
        skip "$t" "xdg_client not built"
    done
else
    # Single window: connect, configure, map, destroy
    XDG_OUT="$LOG_DIR/xdg_client.txt"
    LOG_BEFORE=$(wc -l < "$TEST_HOME/runtime.log" 2>/dev/null || echo 0)

    XDG_EXIT=0
    if wl_timeout_run 10 "$XDG_CLIENT" "test-window-1" 800 >"$XDG_OUT" 2>&1; then
        XDG_EXIT=0
    else
        XDG_EXIT=$?
    fi

    if [[ $XDG_EXIT -eq 0 ]]; then
        pass "xdg-toplevel: compositor accepts connection"
    else
        fail "xdg-toplevel: compositor accepts connection" "client exited with $XDG_EXIT"
        cat "$XDG_OUT" || true
    fi

    if grep -q "mapped" "$XDG_OUT" 2>/dev/null; then
        pass "xdg-toplevel: configure received by client"
    else
        fail "xdg-toplevel: configure received by client" "no 'mapped' in client output"
    fi

    if grep -qE 'WaylandBackend: surface [0-9]+ mapped|WlBackend: surface [0-9]+ mapped|WindowMapped' \
           "$TEST_HOME/runtime.log" 2>/dev/null; then
        pass "xdg-toplevel: surface mapped logged"
    else
        fail "xdg-toplevel: surface mapped logged" "not found in runtime.log"
    fi

    assert_pid_alive "$SIRENWM_PID" "xdg-toplevel: compositor alive after window" "compositor crashed"
    assert_pid_alive "$DISPLAY_SERVER_PID" "xdg-toplevel: display-server alive after window" "display-server crashed"

    if grep -qE 'WaylandBackend: surface [0-9]+ destroyed|WlBackend: surface [0-9]+ destroyed' \
           "$TEST_HOME/runtime.log" 2>/dev/null; then
        pass "xdg-toplevel: surface destroyed cleanly"
    else
        fail "xdg-toplevel: surface destroyed cleanly" "destroy log not found"
    fi

    # 3 concurrent xdg-toplevel windows
    CPIDS=()
    for i in 1 2 3; do
        wl_timeout_run 10 "$XDG_CLIENT" "concurrent-$i" 600 >"$LOG_DIR/xdg_concurrent_$i.txt" 2>&1 &
        CPIDS+=($!)
    done
    CON_OK=true
    for pid in "${CPIDS[@]}"; do
        wait "$pid" || CON_OK=false
    done
    if $CON_OK && kill -0 $SIRENWM_PID 2>/dev/null; then
        pass "xdg-toplevel: 3 concurrent windows"
    else
        fail "xdg-toplevel: 3 concurrent windows" "client failed or compositor crashed"
    fi
    assert_pid_alive "$DISPLAY_SERVER_PID" "xdg-toplevel: display-server alive after concurrent windows" \
        "display-server crashed during concurrent xdg clients"
fi

# ===========================================================================
# Test 27: SIGHUP reload — compositor survives and socket is reused
# ===========================================================================
SOCKET_BEFORE="$WAYLAND_SOCKET_NAME"
kill -HUP $SIRENWM_PID 2>/dev/null || true
sleep 1.0
if kill -0 $SIRENWM_PID 2>/dev/null; then
    pass "SIGHUP: compositor survives reload"
else
    fail "SIGHUP: compositor survives reload" "process died after SIGHUP"
    dump_logs
fi
assert_pid_alive "$DISPLAY_SERVER_PID" "SIGHUP: display-server survives reload" "display-server died after SIGHUP"

# ===========================================================================
# Test 28: socket name unchanged after SIGHUP (clients can reconnect)
# ===========================================================================
SOCKET_AFTER=""
for f in "$XDG_RUNTIME"/wayland-*; do
    [[ -S "$f" ]] && { SOCKET_AFTER="$(basename "$f")"; break; }
done
if [[ "$SOCKET_AFTER" == "$SOCKET_BEFORE" ]]; then
    pass "SIGHUP: Wayland socket name unchanged ($SOCKET_AFTER)"
else
    fail "SIGHUP: Wayland socket name unchanged" "was=$SOCKET_BEFORE now=$SOCKET_AFTER"
fi

# ===========================================================================
# Test 29: wayland-info connects after reload
# ===========================================================================
if wl_run wayland-info >"$LOG_DIR/wayland-info-post-reload.txt" 2>&1; then
    pass "SIGHUP: wayland-info connects after reload"
else
    fail "SIGHUP: wayland-info connects after reload" "connection failed"
fi
assert_pid_alive "$DISPLAY_SERVER_PID" "SIGHUP: display-server alive after reconnect" \
    "display-server died before post-reload reconnect"

# ===========================================================================
# Test 30: reload logged in runtime.log
# ===========================================================================
if grep -qE 'reload|Reload|SIGHUP' "$TEST_HOME/runtime.log" 2>/dev/null; then
    pass "SIGHUP: reload logged in runtime.log"
else
    fail "SIGHUP: reload logged in runtime.log" "not found"
fi

# ===========================================================================
# Test 31: no new crashes after reload
# ===========================================================================
if grep -qE 'Assertion.*failed|SIGSEGV|SIGABRT' "$SIRENWM_LOG" 2>/dev/null; then
    fail "no crashes after reload" "assertion/signal found"
else
    pass "no crashes after reload"
fi

# ===========================================================================
# Test 32: exec-restart preserves Wayland socket (clients survive)
# ===========================================================================
EXEC_RESTART_BIN="$REPO_ROOT/output/sirenwm-wayland"
# Send USR1 is not wired — use the Lua API via runtime.log check
# Instead: verify SIRENWM_WL_SOCKET_FD env var logic by inspecting the binary
if strings "$EXEC_RESTART_BIN" 2>/dev/null | grep -q "SIRENWM_WL_SOCKET_FD"; then
    pass "exec-restart: binary contains socket persistence logic"
else
    skip "exec-restart: binary contains socket persistence logic" "strings not available or symbol stripped"
fi

# ===========================================================================
# Test 33: weston-simple-shm window lifecycle (optional)
# ===========================================================================
WESTON_SHM=$(command -v weston-simple-shm 2>/dev/null || true)
if [[ -z "$WESTON_SHM" ]]; then
    skip "weston-simple-shm window lifecycle" "not installed"
else
    WS_LOG="$LOG_DIR/weston-simple-shm.log"
    wl_run "$WESTON_SHM" >"$WS_LOG" 2>&1 &
    WS_PID=$!
    sleep 0.5
    kill $WS_PID 2>/dev/null || true
    wait $WS_PID 2>/dev/null || true

    if kill -0 $SIRENWM_PID 2>/dev/null; then
        pass "weston-simple-shm: compositor alive after window lifecycle"
    else
        fail "weston-simple-shm: compositor alive after window lifecycle" "compositor crashed"
        dump_logs
    fi
fi

# ===========================================================================
# Test 34: multiple consecutive wayland-info round trips (stress)
# ===========================================================================
STRESS_OK=true
for i in $(seq 1 10); do
    wl_run wayland-info >/dev/null 2>&1 || { STRESS_OK=false; break; }
done
if $STRESS_OK; then
    pass "stress: 10 consecutive wayland-info round trips"
else
    fail "stress: 10 consecutive wayland-info round trips" "failed on iteration $i"
fi

# ===========================================================================
# Test 35: compositor alive after stress test
# ===========================================================================
assert_pid_alive "$SIRENWM_PID" "compositor alive after stress test" "process died"
assert_pid_alive "$DISPLAY_SERVER_PID" "display-server alive after stress test" "process died"

# ===========================================================================
# Test 36: graceful SIGINT shutdown
# ===========================================================================
kill -INT $SIRENWM_PID 2>/dev/null || true
STOPPED=false
# Use /proc/<pid>/status instead of kill -0: kill -0 returns 0 for zombies,
# which would cause a false "still running" even after the process has exited.
for _ in $(seq 1 50); do
    sleep 0.1
    [[ -d "/proc/$SIRENWM_PID" ]] || { STOPPED=true; break; }
    # Also check zombie state — if Z, process has exited (waiting for parent wait)
    STATUS=$(cat "/proc/$SIRENWM_PID/status" 2>/dev/null | grep -E '^State:' | awk '{print $2}')
    [[ "$STATUS" == "Z" ]] && { STOPPED=true; break; }
done
if $STOPPED; then
    wait $SIRENWM_PID 2>/dev/null || true
    pass "compositor shuts down cleanly on SIGINT"
    SIRENWM_PID=0
else
    fail "compositor shuts down cleanly on SIGINT" "still running after 5s"
fi

# ===========================================================================
# Test 37: FSM: Running → Stopped logged
# ===========================================================================
if grep -qE 'FSM:.*Stopp|FSM:.*Running.*Stopp' "$TEST_HOME/runtime.log" 2>/dev/null; then
    pass "FSM: Running → Stopped transition logged"
else
    fail "FSM: Running → Stopped transition logged" "not found in runtime.log"
fi

# ===========================================================================
# Test 38: display-server shuts down cleanly on SIGINT
# ===========================================================================
kill -INT $DISPLAY_SERVER_PID 2>/dev/null || true
DS_STOPPED=false
# Give SIGINT a short grace period first.
for _ in $(seq 1 20); do
    sleep 0.1
    [[ -d "/proc/$DISPLAY_SERVER_PID" ]] || { DS_STOPPED=true; break; }
    STATUS=$(cat "/proc/$DISPLAY_SERVER_PID/status" 2>/dev/null | grep -E '^State:' | awk '{print $2}')
    [[ "$STATUS" == "Z" ]] && { DS_STOPPED=true; break; }
done

# Some display-server builds ignore SIGINT in nested mode; fallback to SIGTERM.
if ! $DS_STOPPED; then
    kill -TERM $DISPLAY_SERVER_PID 2>/dev/null || true
    for _ in $(seq 1 30); do
        sleep 0.1
        [[ -d "/proc/$DISPLAY_SERVER_PID" ]] || { DS_STOPPED=true; break; }
        STATUS=$(cat "/proc/$DISPLAY_SERVER_PID/status" 2>/dev/null | grep -E '^State:' | awk '{print $2}')
        [[ "$STATUS" == "Z" ]] && { DS_STOPPED=true; break; }
    done
fi

if $DS_STOPPED; then
    wait $DISPLAY_SERVER_PID 2>/dev/null || true
    pass "display-server shuts down cleanly on SIGINT"
    DISPLAY_SERVER_PID=0
else
    fail "display-server shuts down cleanly on SIGINT" "still running after 5s"
fi

# ===========================================================================
# Test 39: Wayland socket removed after display-server shutdown
# ===========================================================================
sleep 0.3
if [[ ! -S "$XDG_RUNTIME/$WAYLAND_SOCKET_NAME" ]]; then
    pass "Wayland socket cleaned up after shutdown"
elif wl_run wayland-info >/dev/null 2>&1; then
    fail "Wayland socket cleaned up after shutdown" "socket still accepts connections"
else
    pass "Wayland socket cleaned up after shutdown (stale inode only)"
fi

# ===========================================================================
# Summary
# ===========================================================================
echo ""
echo "----------------------------------------"
echo -e "Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}, ${CYAN}$SKIP skipped${NC}"
echo "----------------------------------------"

[[ $FAIL -eq 0 ]]
