#pragma once

#include <cstdint>
#include <string>

#include <backend/events.hpp>

// Window type derived from _NET_WM_WINDOW_TYPE and WM_TRANSIENT_FOR hints.
// Replaces individual wm_type_* flags.
enum class WindowType {
    Normal,   // regular application window
    Dialog,   // transient dialog (_NET_WM_WINDOW_TYPE_DIALOG or WM_TRANSIENT_FOR set)
    Utility,  // tool window (_NET_WM_WINDOW_TYPE_UTILITY)
    Splash,   // splash screen (_NET_WM_WINDOW_TYPE_SPLASH)
    Modal,    // modal dialog
    Dock,     // panel/taskbar (_NET_WM_WINDOW_TYPE_DOCK)
    Desktop,  // desktop window (_NET_WM_WINDOW_TYPE_DESKTOP)
};

struct WindowState {
    WindowId    id                         = NO_WINDOW;

    bool        visible                    = false;
    bool        floating                   = false;
    bool        fullscreen                 = false;
    bool        borderless                 = false;
    bool        floating_before_fullscreen = false;
    uint32_t    border_before_fullscreen   = 0;
    bool        suppress_focus_once        = false;
    bool        hidden_by_workspace        = false;

    std::string wm_instance;
    std::string wm_class;
    WindowType  type                    = WindowType::Normal;
    bool        wm_fixed_size           = false;
    bool        wm_never_focus          = false; // WM_HINTS.input == False
    bool        wm_static_gravity       = false; // WM_NORMAL_HINTS win_gravity == StaticGravity
    bool        wm_no_decorations       = false; // _MOTIF_WM_HINTS decorations == 0
    bool        fullscreen_self_managed = false; // client owns geometry (pre-map _NET_WM_STATE_FULLSCREEN)
    bool        promote_to_borderless   = false; // set by core at MapRequest: backend should make this borderless
    int         ignore_unmap_count      = 0;     // WM-initiated unmaps pending; suppress UnmapNotify

    int32_t     x            = 0;
    int32_t     y            = 0;
    uint32_t    width        = 0;
    uint32_t    height       = 0;
    uint32_t    border_width = 0;

    bool is_dialog() const { return type == WindowType::Dialog || type == WindowType::Modal; }
};