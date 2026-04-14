#pragma once

#include <support/message.hpp>
#include <domain/window_state.hpp>

// ---------------------------------------------------------------------------
// System tray protocol.
//
// Contract between a system-tray host module (typically the bar) and a
// backend that implements the freedesktop/XEMBED system-tray spec. The
// core is deliberately ignorant of tray semantics — messages travel
// through event::CustomEvent envelopes.
//
// Participants:
//   - Publisher: any backend that detects a tray icon reparenting back
//     to root (e.g. after a MANAGER broadcast). X11 backend publishes
//     from ReparentNotify.
//   - Subscriber: any module that owns a tray host surface (bar module
//     by default). Subscribers adopt the icon and route it to a monitor.
// ---------------------------------------------------------------------------

namespace protocol::system_tray {

// A new tray icon window has appeared and wants to be adopted.
struct IconDocked {
    static constexpr uint32_t kTag = fnv1a("system_tray:icon_docked");
    WindowId                  icon;
};
static_assert(Message<IconDocked>);

} // namespace protocol::system_tray
