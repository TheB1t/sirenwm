#!/usr/bin/env bash
# Shared assertion helpers for integration tests.

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "${GREEN}PASS${NC} $1"; ((++PASS)); }
fail() { echo -e "${RED}FAIL${NC} $1: $2"; ((++FAIL)); }
info() { echo -e "${YELLOW}INFO${NC} $1"; }
skip() { echo -e "${YELLOW}SKIP${NC} $1"; }

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

assert_eq() {
    local desc="$1" got="$2" expected="$3"
    if [[ "$got" == "$expected" ]]; then
        pass "$desc"
    else
        fail "$desc" "got '$got', expected '$expected'"
    fi
}

assert_ne() {
    local desc="$1" got="$2" unexpected="$3"
    if [[ "$got" != "$unexpected" ]]; then
        pass "$desc"
    else
        fail "$desc" "got '$got', expected anything else"
    fi
}

assert_contains() {
    local desc="$1" haystack="$2" needle="$3"
    if echo "$haystack" | grep -q "$needle"; then
        pass "$desc"
    else
        fail "$desc" "'$needle' not found"
    fi
}

assert_not_contains() {
    local desc="$1" haystack="$2" needle="$3"
    if ! echo "$haystack" | grep -q "$needle"; then
        pass "$desc"
    else
        fail "$desc" "'$needle' unexpectedly found"
    fi
}

assert_viewable() {
    local desc="$1" wid="$2"
    if is_window_viewable "$wid"; then
        pass "$desc"
    else
        fail "$desc" "window $wid is not viewable"
    fi
}

assert_not_viewable() {
    local desc="$1" wid="$2"
    if ! is_window_viewable "$wid"; then
        pass "$desc"
    else
        fail "$desc" "window $wid is unexpectedly viewable"
    fi
}

assert_alive() {
    local desc="$1"
    if kill -0 $SIRENWM_PID 2>/dev/null; then
        pass "$desc"
    else
        fail "$desc" "sirenwm process died"
        dump_logs
    fi
}
