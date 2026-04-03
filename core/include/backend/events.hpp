#pragma once

// ---------------------------------------------------------------------------
// Backend-agnostic event types.
// X11 backend adapter translates xcb_* structs -> these before emitting.
// Core and modules only see these types — no xcb headers required.
//
// Names intentionally avoid Xlib macro collisions (KeyPress, Expose, etc.).
// ---------------------------------------------------------------------------

#include <cstdint>

using WindowId = uint32_t;
static constexpr WindowId NO_WINDOW = 0;

namespace event {

struct WindowMapped { WindowId window; };
// withdrawn=true  → window is being unmanaged (WM_STATE = WithdrawnState)
// withdrawn=false → window is hidden by workspace switch (WM_STATE = IconicState)
struct WindowUnmapped { WindowId window; bool withdrawn = false; };
struct FocusChanged { WindowId window; };        // NO_WINDOW = focus cleared
struct WorkspaceSwitched { int workspace_id; };
struct ExposeWindow { WindowId window; };
struct ManageWindowQuery { WindowId window; bool manage = true; };
struct ApplyWindowRules { WindowId window; bool from_restart = false; };
struct CloseWindowRequest { WindowId window; };
struct RaiseDocks {};
struct DisplayTopologyChanged {};
struct WindowAdopted { WindowId window; bool currently_visible; };

struct KeyPressEv {
    uint16_t mods;
    uint8_t  keycode;
    uint32_t keysym;   // filled by backend (keysym lookup from keycode)
};

struct ButtonEv {
    WindowId window;
    WindowId root;
    int16_t  root_x, root_y;
    int16_t  event_x, event_y;
    uint32_t time;
    uint8_t  button;
    uint16_t state;    // modifier mask
    bool     release;
};

struct MotionEv {
    WindowId window;
    int16_t  root_x, root_y;
    uint16_t state;
};

// Client message: opaque 160-bit payload.
// EWMH module decodes atom values; Core stays ignorant of atom semantics.
struct ClientMessageEv {
    WindowId window;
    uint32_t type;    // atom value
    uint8_t  format;  // 8, 16, or 32
    uint32_t data[5]; // data32 view
};

struct DestroyNotify { WindowId window; };
struct ConfigureNotify { WindowId window; int x, y, w, h; };
struct PropertyNotify { WindowId window; uint32_t atom; };

// Emitted when an unmanaged XEMBED window reparents itself to any window.
// bar module uses this to trigger tray rebalancing after a client docks.
struct TrayIconDocked { WindowId icon; };

// Emitted after a window has been placed on a workspace (rules, move, or initial mapping).
struct WindowAssignedToWorkspace { WindowId window; int workspace_id; };

} // namespace event