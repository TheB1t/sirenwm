#pragma once

// ---------------------------------------------------------------------------
// Synchronous hooks — the third pillar of the WM API alongside commands and
// events.
//
//   command::*  — mutate core state, one authoritative handler (Core),
//                 caller waits for bool result.
//   hook::*     — synchronous filter/query, N subscribers run in order,
//                 caller reads mutated args on return. Subscribers do NOT
//                 mutate core directly — if they need to change state they
//                 call core.dispatch(...) themselves.
//   event::*    — fire-and-forget broadcast, delivered through the event
//                 queue on the next drain pass. No return value.
//
// Hooks exist for the cases where "post an event and wait" would require
// the caller to know about drain_events() — an internal detail that must
// not leak into user code. The canonical example is window-rules: the X11
// map-request handler reads window.floating AFTER Lua rules have had a
// chance to toggle it. That is a filter, not a notification.
//
// Hook args are plain structs. In-fields are inputs the subscriber reads;
// out-fields are results the subscriber may mutate. The registry iterates
// subscribers in registration order; any subscriber may stop the chain by
// setting a stop-flag when the hook defines one.
// ---------------------------------------------------------------------------

#include <backend/events.hpp>  // for WindowId / NO_WINDOW

namespace hook {

// Fired from the X11/Wayland map-request path after the window is known to
// core but before placement decisions are made. Lua `window.rules` handler
// is the primary subscriber; it may call siren.win.set_floating() etc.
// which internally dispatch atom commands against core.
struct WindowRules {
    WindowId window;
    bool     from_restart = false;
};

// Asked once per map-request to decide whether the WM should manage this
// window at all. Subscribers set manage=false to tell the backend to leave
// the window alone (override-redirect-like behavior for specific classes).
struct ShouldManageWindow {
    WindowId window;
    bool     manage = true;
};

// Asked when a client requests close (WM_DELETE_WINDOW path or equivalent).
// Subscribers set handled=true if they took ownership of the close — the
// backend then skips its default behavior.
struct CloseWindow {
    WindowId window;
    bool     handled = false;
};

} // namespace hook
