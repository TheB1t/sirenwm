#pragma once

#include <xcb/xcb.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

// Type-safe XCB event dispatch. Mirrors the MessageTraits + X-macro pattern
// from libipc/message_dispatch.hpp: one X-macro enumerates core X11 event
// structs and the XCB opcode each maps to, a traits specialization exposes
// the opcode at compile time, and dispatch_event() drives a single switch
// that invokes handler.on(const T&) for whichever event type matches.
//
// Extension events (RandR, XKB, XFixes, ...) use dynamic opcode bases and
// do not fit the static map; dispatch_event() returns false on unknown
// opcodes so callers can fall through to extension-aware handling.

namespace xcb {

template <typename Event>
struct EventTraits;

// X(TypeName, XcbOpcode)
// XCB typedefs release variants to the press struct (xcb_key_release_event_t
// == xcb_key_press_event_t, same for button/focus/enter-leave), so only the
// "press" struct is listed. The dispatch switch adds a dedicated case for
// every paired opcode so release events still reach the handler as the same
// struct — the handler distinguishes via response_type when needed.
#define SWM_XCB_CORE_EVENTS(X) \
    X(xcb_key_press_event_t,         XCB_KEY_PRESS) \
    X(xcb_button_press_event_t,      XCB_BUTTON_PRESS) \
    X(xcb_motion_notify_event_t,     XCB_MOTION_NOTIFY) \
    X(xcb_enter_notify_event_t,      XCB_ENTER_NOTIFY) \
    X(xcb_focus_in_event_t,          XCB_FOCUS_IN) \
    X(xcb_expose_event_t,            XCB_EXPOSE) \
    X(xcb_graphics_exposure_event_t, XCB_GRAPHICS_EXPOSURE) \
    X(xcb_no_exposure_event_t,       XCB_NO_EXPOSURE) \
    X(xcb_visibility_notify_event_t, XCB_VISIBILITY_NOTIFY) \
    X(xcb_create_notify_event_t,     XCB_CREATE_NOTIFY) \
    X(xcb_destroy_notify_event_t,    XCB_DESTROY_NOTIFY) \
    X(xcb_unmap_notify_event_t,      XCB_UNMAP_NOTIFY) \
    X(xcb_map_notify_event_t,        XCB_MAP_NOTIFY) \
    X(xcb_map_request_event_t,       XCB_MAP_REQUEST) \
    X(xcb_reparent_notify_event_t,   XCB_REPARENT_NOTIFY) \
    X(xcb_configure_notify_event_t,  XCB_CONFIGURE_NOTIFY) \
    X(xcb_configure_request_event_t, XCB_CONFIGURE_REQUEST) \
    X(xcb_property_notify_event_t,   XCB_PROPERTY_NOTIFY) \
    X(xcb_client_message_event_t,    XCB_CLIENT_MESSAGE) \
    X(xcb_ge_generic_event_t,        XCB_GE_GENERIC)

#define SWM_XCB_DECLARE_TRAIT(TypeName, XcbOpcode) \
    template <> \
    struct EventTraits<TypeName> { \
        static constexpr uint8_t opcode = (XcbOpcode); \
    };

SWM_XCB_CORE_EVENTS(SWM_XCB_DECLARE_TRAIT)

#undef SWM_XCB_DECLARE_TRAIT

template <typename T>
concept KnownEvent = requires { EventTraits<T>::opcode; };

// Safe, intention-revealing cast from the erased xcb_generic_event_t* to a
// concrete event struct. Equivalent to reinterpret_cast but constrained to
// one of the enumerated event structs, preventing silent misuse on
// arbitrary types.
template <KnownEvent T>
T* event_as(xcb_generic_event_t* ev) {
    return reinterpret_cast<T*>(ev);
}

template <KnownEvent T>
const T* event_as(const xcb_generic_event_t* ev) {
    return reinterpret_cast<const T*>(ev);
}

namespace detail {

template <typename Handler, typename Event>
concept HandlesEventBool =
    requires(Handler&& h, Event & ev) {
    { std::forward<Handler>(h).on(ev) }->std::convertible_to<bool>;
};

template <typename Handler, typename Event>
concept HandlesEventVoid =
    requires(Handler&& h, Event & ev) {
    { std::forward<Handler>(h).on(ev) }->std::same_as<void>;
};

// Invoke handler.on(ev) if defined; return true if handled, false if the
// handler has no overload for this event type (SFINAE-miss at compile time).
template <typename Event, typename Handler>
bool invoke(Handler&& h, Event& ev) {
    if constexpr (HandlesEventBool<Handler, Event>) {
        return static_cast<bool>(std::forward<Handler>(h).on(ev));
    } else if constexpr (HandlesEventVoid<Handler, Event>) {
        std::forward<Handler>(h).on(ev);
        return true;
    } else {
        return false;
    }
}

} // namespace detail

// Dispatch a raw XCB event to a handler that defines `on(const T&)` /
// `on(T&)` overloads for the event types it cares about. Returns true if
// the opcode matched a known core event (whether or not the handler had an
// overload — an opcode with no overload still counts as "recognized", so
// callers can silence default-case logging). Returns false for:
//   - error replies (opcode 0)
//   - extension events (dynamic opcode base)
//   - unknown opcodes
// The caller is responsible for these three cases.
template <typename Handler>
bool dispatch_event(xcb_generic_event_t* ev, Handler&& handler) {
    const uint8_t type = ev->response_type & 0x7f;
    switch (type) {
#define SWM_XCB_DISPATCH_CASE(TypeName, XcbOpcode) \
    case (XcbOpcode): \
        detail::invoke<TypeName>(std::forward<Handler>(handler), \
            *reinterpret_cast<TypeName*>(ev)); \
        return true;

    SWM_XCB_CORE_EVENTS(SWM_XCB_DISPATCH_CASE)

        // Release variants share the press struct typedef — route them to
        // the same handler overload. The handler uses response_type to tell
        // press vs release apart when it cares.
        case XCB_KEY_RELEASE:
            detail::invoke<xcb_key_press_event_t>(std::forward<Handler>(handler),
                *reinterpret_cast<xcb_key_press_event_t*>(ev));
            return true;
        case XCB_BUTTON_RELEASE:
            detail::invoke<xcb_button_press_event_t>(std::forward<Handler>(handler),
                *reinterpret_cast<xcb_button_press_event_t*>(ev));
            return true;
        case XCB_FOCUS_OUT:
            detail::invoke<xcb_focus_in_event_t>(std::forward<Handler>(handler),
                *reinterpret_cast<xcb_focus_in_event_t*>(ev));
            return true;
        case XCB_LEAVE_NOTIFY:
            detail::invoke<xcb_enter_notify_event_t>(std::forward<Handler>(handler),
                *reinterpret_cast<xcb_enter_notify_event_t*>(ev));
            return true;

#undef SWM_XCB_DISPATCH_CASE
        default:
            return false;
    }
}

} // namespace xcb
