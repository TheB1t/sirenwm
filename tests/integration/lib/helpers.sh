#!/usr/bin/env bash
# Shared X11 query helpers for integration tests.

D() { DISPLAY=$DISPLAY_NUM "$@"; }

wait_for_display() {
    local disp="$1" n=0
    while ! DISPLAY=$disp xdpyinfo &>/dev/null; do
        sleep 0.1; ((++n))
        if (( n > 60 )); then
            echo -e "${RED}TIMEOUT${NC} waiting for display $disp"
            return 1
        fi
    done
}

wait_for_swm() {
    local disp="$1" n=0
    while ! DISPLAY=$disp xprop -root _NET_SUPPORTED &>/dev/null 2>&1; do
        sleep 0.1; ((++n))
        if (( n > 60 )); then
            echo -e "${RED}TIMEOUT${NC} waiting for sirenwm on $disp"
            return 1
        fi
    done
}

get_geom() {
    local class="$1"
    D xwininfo -root -tree 2>/dev/null \
        | grep -i "$class" | head -1 \
        | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1 || true
}

get_window_id() {
    local pattern="$1"
    D xwininfo -root -tree 2>/dev/null \
        | grep -i "$pattern" | head -1 | awk '{print $1}' || true
}

get_window_ids() {
    local pattern="$1" max="${2:-10}"
    D xwininfo -root -tree 2>/dev/null \
        | grep -i "$pattern" | awk '{print $1}' | head -"$max"
}

wait_for_window() {
    local pattern="$1" timeout="${2:-40}" wid="" n=0
    while (( n < timeout )); do
        wid="$(get_window_id "$pattern")"
        [[ -n "$wid" ]] && echo "$wid" && return 0
        sleep 0.1; ((++n))
    done
    return 1
}

is_window_viewable() {
    local wid="$1"
    D xwininfo -id "$wid" 2>/dev/null | grep -q "Map State: IsViewable"
}

current_desktop() {
    D wmctrl -d 2>/dev/null | awk '$2 == "*" { print $1; exit }'
}

active_window() {
    D xprop -root _NET_ACTIVE_WINDOW 2>/dev/null \
        | awk '{print $NF}' | tr 'A-Z' 'a-z'
}

wait_for_desktop() {
    local expected="$1" n=0
    while true; do
        local cur; cur="$(current_desktop)"
        [[ "$cur" == "$expected" ]] && return 0
        sleep 0.1; ((++n))
        (( n > 50 )) && return 1
    done
}

normalize_wid() {
    local raw; raw="$(echo "$1" | tr 'A-Z' 'a-z')"
    echo "$raw" | sed 's/^0x0*/0x/'
}

wait_for_active_window() {
    local expected; expected="$(normalize_wid "$1")"
    local n=0
    while true; do
        local cur; cur="$(normalize_wid "$(active_window)")"
        [[ "$cur" == "$expected" ]] && return 0
        sleep 0.1; ((++n))
        (( n > 50 )) && return 1
    done
}

geom_w() { echo "$1" | grep -oE '^[0-9]+' || true; }
geom_h() { echo "$1" | sed 's/^[0-9]*x//;s/+.*//' || true; }
geom_x() { echo "$1" | grep -oE '\+[0-9]+\+' | head -1 | tr -d '+' || true; }
geom_y() { echo "$1" | grep -oE '\+[0-9]+$' | tr -d '+' || true; }

xprop_root() { D xprop -root "$@" 2>/dev/null; }
xprop_win()  { local wid="$1"; shift; D xprop -id "$wid" "$@" 2>/dev/null; }

window_prop() {
    local wid="$1" prop="$2"
    xprop_win "$wid" "$prop"
}

root_prop() {
    local prop="$1"
    xprop_root "$prop"
}

spawn_xclock() {
    local name="${1:-xclock_test}" geom="${2:-100x100}"
    DISPLAY=$DISPLAY_NUM xclock -name "$name" -geometry "$geom" &
    SPAWNED_PIDS+=($!)
}

spawn_xterm() {
    local title="${1:-xterm_test}" name="${2:-$1}"
    DISPLAY=$DISPLAY_NUM xterm -title "$title" -name "$name" &
    SPAWNED_PIDS+=($!)
}

cleanup_spawned() {
    for pid in "${SPAWNED_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    SPAWNED_PIDS=()
    sleep 0.3
}

dump_logs() {
    info "sirenwm log: $SIRENWM_LOG"
    tail -n 60 "$SIRENWM_LOG" 2>/dev/null || true
    info "Xephyr log: $XEPHYR_LOG"
    tail -n 60 "$XEPHYR_LOG" 2>/dev/null || true
}

switch_ws() {
    local ws="$1"
    D wmctrl -s "$ws"
    wait_for_desktop "$ws" || true
    sleep 0.1
}

focus_window() {
    local wid="$1"
    D wmctrl -i -a "$wid"
    wait_for_active_window "$wid" || true
    sleep 0.1
}

find_dock_window() {
    local bar_id=""
    for _ in $(seq 1 20); do
        while IFS= read -r wid; do
            [[ "$wid" == 0x* ]] || continue
            local wtype; wtype=$(xprop_win "$wid" _NET_WM_WINDOW_TYPE)
            if echo "$wtype" | grep -q "WINDOW_TYPE_DOCK"; then
                echo "$wid"; return 0
            fi
        done < <(D xwininfo -root -tree 2>/dev/null | awk '/^\s+0x/ {print $1}')
        sleep 0.1
    done
    return 1
}

client_list_count() {
    local raw
    raw=$(root_prop _NET_CLIENT_LIST 2>/dev/null | head -1 || true)
    local matches
    matches=$(echo "$raw" | grep -oE '0x[0-9a-fA-F]+' || true)
    if [[ -z "$matches" ]]; then
        echo 0
    else
        echo "$matches" | wc -l
    fi
}
