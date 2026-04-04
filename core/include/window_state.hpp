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

// Window presentation intent derived from hints at map time.
// Drives layout and decoration policy; replaces fullscreen_self_managed + wm_no_decorations.
enum class WindowIntent {
    Normal,      // tiled/managed normally
    Floating,    // floating, managed by WM geometry
    Fullscreen,  // fullscreen managed by WM (WM sets geometry + hides decorations)
    Borderless,  // fullscreen self-managed (client owns geometry, WM only tracks it)
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
    WindowIntent intent                 = WindowIntent::Normal;
    bool        wm_fixed_size           = false;
    bool        wm_never_focus          = false; // WM_HINTS.input == False
    bool        wm_static_gravity       = false; // WM_NORMAL_HINTS win_gravity == StaticGravity
    bool        wm_no_decorations       = false; // _MOTIF_WM_HINTS decorations == 0 (raw hint, kept for EWMH reactions)
    bool        fullscreen_self_managed = false; // intent == Borderless shorthand (kept during migration)
    int         ignore_unmap_count      = 0;     // WM-initiated unmaps pending; suppress UnmapNotify

    int32_t     x            = 0;
    int32_t     y            = 0;
    uint32_t    width        = 0;
    uint32_t    height       = 0;
    uint32_t    border_width = 0;
    uint32_t    sibling      = 0;
    uint32_t    stack_mode   = 0;
    uint32_t    event_mask   = 0;

    // Convenience queries
    bool is_dialog() const  { return type == WindowType::Dialog || type == WindowType::Modal; }
    bool is_borderless_intent() const { return intent == WindowIntent::Borderless; }
    bool is_fullscreen_intent() const { return intent == WindowIntent::Fullscreen; }
};