#pragma once

#include <cstdint>
#include <string>

#include <backend/events.hpp>
#include <vec.hpp>

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
    WindowId    id = NO_WINDOW;

    bool        mapped                   = false;   // window is mapped in X11 (MapNotify received)
    bool        hidden_by_workspace      = false;   // hidden because its workspace is inactive
    bool        hidden_explicitly        = false;   // hidden via HideWindow command
    bool        floating                 = false;
    bool        fullscreen               = false;
    bool        borderless               = false;
    uint32_t    border_before_fullscreen = 0;
    Vec2i       pos_before_fullscreen;
    Vec2i       size_before_fullscreen;
    bool        suppress_focus_once = false;

    std::string wm_instance;
    std::string wm_class;
    std::string title;
    uint32_t    pid               = 0;
    WindowType  type              = WindowType::Normal;
    bool        size_locked       = false;       // WM_NORMAL_HINTS min == max
    bool        no_input_focus    = false;      // WM_HINTS.input == False
    bool        urgent            = false;      // WM_HINTS UrgencyHint
    bool        preserve_position = false;      // client wants to keep physical coordinates (X11: StaticGravity)

    // WM_NORMAL_HINTS size constraints (used when floating).
    Vec2i    size_min;      // min size (0,0 = unconstrained)
    Vec2i    size_max;      // max size (0,0 = unconstrained)
    Vec2i    size_inc;      // resize increment (terminal cell size); (0,0 = any)
    Vec2i    size_base;     // base size subtracted before applying increments
    bool     self_managed          = false;      // client owns geometry; WM must not override position/size
    bool     promote_to_borderless = false;      // set by core at MapRequest: backend should make this borderless

    Vec2i    pos_;
    Vec2i    size_;
    uint32_t border_width = 0;

    // Scalar accessors (backward compat)
    int&       x()             { return pos_.x(); }
    int        x()       const { return pos_.x(); }
    int&       y()             { return pos_.y(); }
    int        y()       const { return pos_.y(); }
    int&       width()         { return size_.x(); }
    int        width()   const { return size_.x(); }
    int&       height()        { return size_.y(); }
    int        height()  const { return size_.y(); }

    // Vec2 accessors
    Vec2i&       pos()        { return pos_; }
    const Vec2i& pos()  const { return pos_; }
    Vec2i&       size()       { return size_; }
    const Vec2i& size() const { return size_; }

    Vec2i center() const { return pos_ + size_ / 2; }

    bool is_visible()      const { return mapped && !hidden_by_workspace && !hidden_explicitly; }
    bool is_dialog()       const { return type == WindowType::Dialog || type == WindowType::Modal; }
    bool is_self_managed() const { return self_managed || preserve_position; }
};
