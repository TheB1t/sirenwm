#pragma once

#include <cstdint>
#include <string>

#include <backend/events.hpp>

struct WindowState {
    WindowId    id                         = NO_WINDOW;

    bool        visible                    = false;
    bool        floating                   = false;
    bool        fullscreen                 = false;
    bool        borderless                 = false; // _MOTIF_WM_HINTS no-decorations, layout skips, geometry self-managed
    bool        floating_before_fullscreen = false;
    uint32_t    border_before_fullscreen   = 0;
    bool        suppress_focus_once        = false;
    bool        hidden_by_workspace        = false;

    std::string wm_instance;
    std::string wm_class;
    bool        wm_type_dialog     = false;
    bool        wm_type_utility    = false;
    bool        wm_type_splash     = false;
    bool        wm_type_modal      = false;
    bool        wm_fixed_size      = false;
    bool        wm_never_focus     = false; // WM_HINTS.input == False
    bool        wm_static_gravity  = false; // WM_NORMAL_HINTS win_gravity == StaticGravity
    bool        wm_no_decorations  = false; // _MOTIF_WM_HINTS decorations == 0
    bool        fullscreen_self_managed = false; // had _NET_WM_STATE_FULLSCREEN before MapRequest
    int         ignore_unmap_count = 0; // WM-initiated unmaps pending; suppress UnmapNotify

    int32_t     x            = 0;
    int32_t     y            = 0;
    uint32_t    width        = 0;
    uint32_t    height       = 0;
    uint32_t    border_width = 0;
    uint32_t    sibling      = 0;
    uint32_t    stack_mode   = 0;
    uint32_t    event_mask   = 0;
};