#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct MonitorAlias {
    std::string alias;       // logical name used in workspace defs, e.g. "primary"
    std::string output;      // RandR output name, e.g. "eDP-1", "HDMI-1"
    int         width   = 0; // native/output mode width
    int         height  = 0; // native/output mode height
    int         rate    = 0; // refresh rate Hz, 0 = best available for width/height
    std::string rotation;    // required: "normal", "left", "right", "inverted"
    bool        enabled = true;
};

struct MonitorComposeLink {
    std::string monitor;      // monitor alias from siren.monitors
    std::string relative_to;  // monitor alias this placement is relative to; empty only for primary
    std::string side;         // "left" | "right" | "top" | "bottom"; empty only for primary
    int         shift = 0;    // axis shift: Y for left/right, X for top/bottom
};

struct MonitorCompose {
    std::string                     primary; // alias of primary monitor
    std::vector<MonitorComposeLink> layout; // placement graph
    bool                            defined = false;
};

struct WorkspaceDef {
    std::string name;    // display name, e.g. "[2] Browser"
    std::string monitor; // monitor alias; empty = any available
};

struct WindowRule {
    std::string class_name;    // WM_CLASS class
    std::string instance_name; // WM_CLASS instance
    int         workspace  = -1; // 0-indexed; -1 = no workspace assignment
    bool        isfloating = false;
};

struct ThemeConfig {
    int         dpi         = 0;   // 0 = do not set
    int         cursor_size = 0;   // 0 = do not set
    std::string cursor_theme;      // empty = do not set

    std::string font;    // default UI font (e.g. "Terminus:size=10"); modules fall back to this

    // 5 base colors — modules use these as fallback if their own colors are not set
    std::string bg;      // main background
    std::string fg;      // main foreground / text
    std::string alt_bg;  // alternate background (panels, inactive elements)
    std::string alt_fg;  // text on alt_bg / focused elements
    std::string accent;  // accent / active / focused element

    // Window decoration
    int         border_thickness = 1;
    std::string border_focused;   // defaults to accent
    std::string border_unfocused; // defaults to alt_bg
    int         gap = 4;
};