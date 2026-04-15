#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

FAIL=0

note() {
    printf 'INFO %s\n' "$*"
}

fail() {
    printf 'ERROR %s\n' "$*" >&2
    FAIL=1
}

check_no_include_pattern() {
    local name="$1"
    local pattern="$2"
    shift 2
    local paths=("$@")

    local matches
    if ! matches=$(rg -n "$pattern" "${paths[@]}" -g '*.[ch]pp' -g '*.hpp' 2>/dev/null); then
        return 0
    fi

    fail "$name"
    printf '%s\n' "$matches" >&2
}

check_file_absent_pattern() {
    local file="$1"
    local pattern="$2"
    local name="$3"

    if rg -n "$pattern" "$file" >/dev/null 2>&1; then
        fail "$name"
        rg -n "$pattern" "$file" >&2 || true
    fi
}

note "checking WM-side code does not include wlserver headers"
check_no_include_pattern \
    "WM-side sources must not include wlserver headers (wl/*)" \
    '^[[:space:]]*#include[[:space:]]*[<"]wl/' \
    core modules backends/wayland
check_no_include_pattern \
    "Non-X11 WM-side sources must not include X11 backend-private headers (x11/*)" \
    '^[[:space:]]*#include[[:space:]]*[<"]x11/' \
    core modules backends/wayland

note "checking core and modules do not depend on IPC transport types"
check_no_include_pattern \
    "core/ and modules/ must not include swm/ipc headers" \
    '^[[:space:]]*#include[[:space:]]*[<"]swm/ipc/' \
    core modules

note "checking X11 backend stays independent from Wayland and IPC layers"
check_no_include_pattern \
    "backends/x11 must not include wlserver headers (wl/*)" \
    '^[[:space:]]*#include[[:space:]]*[<"]wl/' \
    backends/x11
check_no_include_pattern \
    "backends/x11 must not include swm/ipc headers" \
    '^[[:space:]]*#include[[:space:]]*[<"]swm/ipc/' \
    backends/x11

note "checking libipc stays independent from WM/domain/server internals"
check_no_include_pattern \
    "libipc must not include core/runtime/backend/domain/config/lua/support internals" \
    '^[[:space:]]*#include[[:space:]]*[<"](backend/|config/|domain/|runtime/|lua/|support/)' \
    libipc
check_no_include_pattern \
    "libipc must not include wlserver or x11 backend headers" \
    '^[[:space:]]*#include[[:space:]]*[<"](wl/|x11/)' \
    libipc

note "checking wayland backend stays on IPC boundary"
check_file_absent_pattern \
    backends/wayland/backend.cmake \
    '\bwlserver\b' \
    "backends/wayland/backend.cmake must not link wlserver directly"

if (( FAIL != 0 )); then
    exit 1
fi

note "architecture boundary checks passed"
