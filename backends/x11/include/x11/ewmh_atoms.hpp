#pragma once

#include <xcb/atom_registry.hpp>

#include <array>
#include <cstddef>

// Backend-level EWMH/ICCCM atoms interned once at startup by X11Backend.
// Per-window atom operations use a narrower registry (see x11_window_atoms.hpp).

#define SIRENWM_EWMH_ATOMS(X)                                              \
    X(NetSupported,                 "_NET_SUPPORTED")                      \
    X(NetWmName,                    "_NET_WM_NAME")                        \
    X(NetWmState,                   "_NET_WM_STATE")                       \
    X(NetWmStateFullscreen,         "_NET_WM_STATE_FULLSCREEN")            \
    X(NetWmStateHidden,             "_NET_WM_STATE_HIDDEN")                \
    X(NetWmStateFocused,            "_NET_WM_STATE_FOCUSED")               \
    X(NetFrameExtents,              "_NET_FRAME_EXTENTS")                  \
    X(NetActiveWindow,              "_NET_ACTIVE_WINDOW")                  \
    X(NetClientList,                "_NET_CLIENT_LIST")                    \
    X(NetClientListStacking,        "_NET_CLIENT_LIST_STACKING")           \
    X(NetSupportingWmCheck,         "_NET_SUPPORTING_WM_CHECK")            \
    X(NetWmWindowType,              "_NET_WM_WINDOW_TYPE")                 \
    X(NetWmWindowTypeDock,          "_NET_WM_WINDOW_TYPE_DOCK")            \
    X(NetWmWindowTypeDialog,        "_NET_WM_WINDOW_TYPE_DIALOG")          \
    X(NetWmWindowTypeDesktop,       "_NET_WM_WINDOW_TYPE_DESKTOP")         \
    X(NetWmWindowTypeNotification,  "_NET_WM_WINDOW_TYPE_NOTIFICATION")    \
    X(NetWmWindowTypeTooltip,       "_NET_WM_WINDOW_TYPE_TOOLTIP")         \
    X(NetWmWindowTypeDnd,           "_NET_WM_WINDOW_TYPE_DND")             \
    X(NetWmWindowTypeDropdownMenu,  "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU")   \
    X(NetWmWindowTypePopupMenu,     "_NET_WM_WINDOW_TYPE_POPUP_MENU")      \
    X(NetWmWindowTypeMenu,          "_NET_WM_WINDOW_TYPE_MENU")            \
    X(NetCloseWindow,               "_NET_CLOSE_WINDOW")                   \
    X(NetNumberOfDesktops,          "_NET_NUMBER_OF_DESKTOPS")             \
    X(NetCurrentDesktop,            "_NET_CURRENT_DESKTOP")                \
    X(NetDesktopNames,              "_NET_DESKTOP_NAMES")                  \
    X(NetDesktopGeometry,           "_NET_DESKTOP_GEOMETRY")               \
    X(NetDesktopViewport,           "_NET_DESKTOP_VIEWPORT")               \
    X(NetWorkarea,                  "_NET_WORKAREA")                       \
    X(NetWmDesktop,                 "_NET_WM_DESKTOP")                     \
    X(Utf8String,                   "UTF8_STRING")                         \
    X(WmProtocols,                  "WM_PROTOCOLS")                        \
    X(WmDeleteWindow,               "WM_DELETE_WINDOW")                    \
    X(WmTakeFocus,                  "WM_TAKE_FOCUS")                       \
    X(WmState,                      "WM_STATE")                            \
    X(XembedInfo,                   "_XEMBED_INFO")

enum class EwmhAtom : std::size_t {
#define SIRENWM_EWMH_ATOM_ENUM(e, s) e,
    SIRENWM_EWMH_ATOMS(SIRENWM_EWMH_ATOM_ENUM)
#undef SIRENWM_EWMH_ATOM_ENUM
    _Count
};

inline constexpr std::array<const char*, static_cast<std::size_t>(EwmhAtom::_Count)>
kEwmhAtomNames = {
#define SIRENWM_EWMH_ATOM_STR(e, s)  s,
    SIRENWM_EWMH_ATOMS(SIRENWM_EWMH_ATOM_STR)
#undef SIRENWM_EWMH_ATOM_STR
};

using EwmhAtomRegistry = xcb::AtomRegistry<EwmhAtom, static_cast<std::size_t>(EwmhAtom::_Count)>;
