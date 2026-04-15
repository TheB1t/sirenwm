#pragma once

// ---------------------------------------------------------------------------
// Backend-agnostic event types.
// X11 backend adapter translates xcb_* structs -> these before emitting.
// Core and modules only see these types — no xcb headers required.
//
// Names intentionally avoid Xlib macro collisions (KeyPress, Expose, etc.).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>

#include <support/message.hpp>
#include <support/strong_id.hpp>
#include <support/vec.hpp>

struct WindowTag;
// Uses StrongIdCastable because xcb_window_t is threaded through hundreds of
// backend call sites; cross-tag mixing with MonitorId/WorkspaceId remains
// rejected at compile time.
using WindowId = swm::StrongIdCastable<WindowTag, uint32_t>;
inline constexpr WindowId NO_WINDOW{ 0 };

struct WorkspaceTag;
// WorkspaceId is a castable StrongId: used pervasively as an array index
// (0..workspaces.size()-1) with -1 as the "unassigned/invalid" sentinel.
// Implicit int conversion keeps arithmetic and indexing natural while
// preventing confusion with MonitorId / WindowId at compile time.
using WorkspaceId = swm::StrongIdCastable<WorkspaceTag, int>;
inline constexpr WorkspaceId NO_WORKSPACE{ -1 };

namespace event {

struct WindowMapped { WindowId window; };
// withdrawn=true  → window is being unmanaged (WM_STATE = WithdrawnState)
// withdrawn=false → window is hidden by workspace switch (WM_STATE = IconicState)
struct WindowUnmapped { WindowId window; bool withdrawn = false; };
struct FocusChanged { WindowId window; };        // NO_WINDOW = focus cleared
struct WorkspaceSwitched { WorkspaceId workspace_id; };
struct ExposeWindow { WindowId window; };
struct RaiseDocks {};
struct DisplayTopologyChanged {};
struct RuntimeStarted {};   // fired once after start() completes: monitors and windows are ready
struct WindowAdopted { WindowId window; bool currently_visible; };

struct KeyPressEv {
    uint16_t mods;
    uint8_t  keycode;
    uint32_t keysym;   // filled by backend (keysym lookup from keycode)
};

struct ButtonEv {
    WindowId window;
    WindowId root;
    Vec2i16  root_pos;
    Vec2i16  event_pos;
    uint32_t time;
    uint8_t  button;
    uint16_t state;    // modifier mask
    bool     release;
};

struct MotionEv {
    WindowId window;
    Vec2i16  root_pos;
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
struct ConfigureNotify { WindowId window; Vec2i pos; Vec2i size; };
struct PropertyNotify { WindowId window; uint32_t atom; };

// Emitted when an unmanaged XEMBED window reparents itself to any window.
// bar module uses this to trigger tray rebalancing after a client docks.
// Emitted after a window has been placed on a workspace (rules, move, or initial mapping).
struct WindowAssignedToWorkspace { WindowId window; WorkspaceId workspace_id; };

// Lifecycle events — previously ad-hoc string-based emit_to_lua() calls,
// now proper type-safe structs dispatched through IEventReceiver.
struct RuntimeStopping { bool exec_restart = false; };
struct ConfigReloaded {};
struct ChildExited { int pid = 0; int exit_code = 0; };

// Envelope for protocol messages that the core is deliberately ignorant of.
// Any module/backend can emit one; any receiver inspects via msg.unpack<T>()
// for the protocols it cares about. Unknown tags are silently dropped.
// See core/include/protocol/ for the public contract headers.
struct CustomEvent { MessageEnvelope msg; };

} // namespace event
