#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <backend/events.hpp>
#include <domain/monitor.hpp>
#include <support/vec.hpp>
#include <domain/window_state.hpp>

// ---------------------------------------------------------------------------
// Command surface — two typed layers over the core reducer.
//
//   command::atom::      primitive operations on the core model. Executed
//                        directly by the core. Every other command
//                        decomposes into a sequence of atoms.
//
//   command::composite:: sugar scenarios that read state and then emit one
//                        or more atoms. Convenient for call sites that do
//                        not want to orchestrate the read-modify-write dance
//                        themselves.
//
// The core exposes two reducer entry points: `dispatch(CommandAtom)` and
// `dispatch(CommandComposite)`. Each variant is visited to the corresponding
// per-type overload. There is deliberately no "Command" umbrella — callers
// must pick their layer explicitly.
// ---------------------------------------------------------------------------

namespace command {

// Raw observable facts about a window supplied by the backend at map time.
// No policy decisions — just what the protocol says. Shared by atom::
// SetWindowMetadata.
struct WindowHints {
    bool  no_decorations = false; // _MOTIF_WM_HINTS decorations == 0
    bool  fixed_size     = false; // min == max in WM_NORMAL_HINTS
    bool  never_focus    = false; // WM_HINTS.input == False
    bool  urgent         = false; // WM_HINTS UrgencyHint bit set
    bool  static_gravity = false; // WM_NORMAL_HINTS StaticGravity
    bool  covers_monitor = false; // outer size >= any monitor usable area at map time
    bool  pre_fullscreen = false; // had _NET_WM_STATE_FULLSCREEN before MapRequest
    bool  is_xembed      = false; // _XEMBED_INFO present — not self-managed
    Vec2i size_min;               // WM_NORMAL_HINTS PMinSize  (0,0 = none)
    Vec2i size_max;               // WM_NORMAL_HINTS PMaxSize  (0,0 = none)
    Vec2i size_inc;               // WM_NORMAL_HINTS PResizeInc (0,0 = any)
    Vec2i size_base;              // WM_NORMAL_HINTS PBaseSize (0,0 = none)
};

namespace atom {

struct FocusWindow {
    WindowId window = NO_WINDOW;
};

struct SwitchWorkspace {
    WorkspaceId              workspace_id = NO_WORKSPACE;
    std::optional<MonitorId> monitor_index; // nullopt => resolve by policy
};

struct MoveWindowToWorkspace {
    WindowId    window       = NO_WINDOW;
    WorkspaceId workspace_id = NO_WORKSPACE;
};

struct MapWindow {
    WindowId window = NO_WINDOW;
};

struct UnmapWindow {
    WindowId window = NO_WINDOW;
};

struct SetWindowFullscreen {
    WindowId window            = NO_WINDOW;
    bool     enabled           = false;
    bool     preserve_geometry = false; // don't pin x/y/w/h to monitor (self-managed clients)
};

struct EnsureWindow {
    WindowId    window       = NO_WINDOW;
    WorkspaceId workspace_id = NO_WORKSPACE;
};

struct AssignWindowWorkspace {
    WindowId    window       = NO_WINDOW;
    WorkspaceId workspace_id = NO_WORKSPACE;
};

struct SetWindowMetadata {
    WindowId    window = NO_WINDOW;
    std::string wm_instance;
    std::string wm_class;
    std::string title;
    uint32_t    pid  = 0;
    WindowType  type = WindowType::Normal;
    WindowHints hints;
    WindowId    transient_for = NO_WINDOW;
};

struct SetWindowMapped {
    WindowId window = NO_WINDOW;
    bool     mapped = false;
};

struct SetWindowHiddenByWorkspace {
    WindowId window = NO_WINDOW;
    bool     hidden = false;
};

struct SetWindowSuppressFocusOnce {
    WindowId window   = NO_WINDOW;
    bool     suppress = false;
};

struct SetWindowFloating {
    WindowId window   = NO_WINDOW;
    bool     floating = false;
};

struct SetWindowBorderless {
    WindowId window     = NO_WINDOW;
    bool     borderless = false;
};

struct HideWindow {
    WindowId window = NO_WINDOW;
};

struct ApplyMonitorTopology {
    std::vector<Monitor> monitors;
};

// Reserve `px` pixels of space at one edge of a monitor (bars, panels,
// docks). The core subtracts the reservation from the usable workspace
// area and keeps the original physical rect recoverable via Monitor::physical().
struct ReserveMonitorArea {
    MonitorId   monitor_idx = NO_MONITOR; // NO_MONITOR = apply to every monitor
    MonitorEdge edge        = MonitorEdge::Top;
    int         px          = 0;
};

struct SetLayout {
    std::string name;
};

struct SetMasterFactor {
    float value = 0.0f;
};

struct FocusMonitor {
    MonitorId monitor_index = NO_MONITOR;
};

struct MoveWindowToMonitor {
    WindowId  window        = NO_WINDOW;
    MonitorId monitor_index = NO_MONITOR;
};

struct ReconcileNow {};

struct RemoveWindowFromAllWorkspaces {
    WindowId window = NO_WINDOW;
};

struct SetWindowGeometry {
    WindowId window = NO_WINDOW;
    Vec2i    pos;
    Vec2i    size;
};

struct SetWindowPosition {
    WindowId window = NO_WINDOW;
    Vec2i    pos;
};

struct SetWindowSize {
    WindowId window = NO_WINDOW;
    Vec2i    size;
};

struct SetWindowBorderWidth {
    WindowId window       = NO_WINDOW;
    uint32_t border_width = 0;
};

struct SyncWindowFromConfigureNotify {
    WindowId window = NO_WINDOW;
    Vec2i    pos;
    Vec2i    size;
    uint32_t border_width = 0;
};

struct CloseWindow {
    WindowId window = NO_WINDOW;
};

} // namespace atom

namespace composite {

struct ToggleWindowFloating {
    WindowId window = NO_WINDOW;
};

struct MoveFocusedWindowToWorkspace {
    WorkspaceId workspace_id = NO_WORKSPACE;
};

struct FocusNextWindow {};
struct FocusPrevWindow {};
struct ToggleFocusedWindowFloating {};

struct SwitchWorkspaceLocalIndex {
    int local_index = -1;
};

struct AdjustMasterFactor {
    float delta = 0.0f;
};

struct IncMaster {
    int delta = 0;
};

struct Zoom {};

} // namespace composite

using CommandAtom = std::variant<
    atom::FocusWindow,
    atom::SwitchWorkspace,
    atom::MoveWindowToWorkspace,
    atom::MapWindow,
    atom::UnmapWindow,
    atom::SetWindowFullscreen,
    atom::EnsureWindow,
    atom::AssignWindowWorkspace,
    atom::SetWindowMetadata,
    atom::SetWindowMapped,
    atom::SetWindowHiddenByWorkspace,
    atom::SetWindowSuppressFocusOnce,
    atom::SetWindowFloating,
    atom::SetWindowBorderless,
    atom::HideWindow,
    atom::ApplyMonitorTopology,
    atom::ReserveMonitorArea,
    atom::SetLayout,
    atom::SetMasterFactor,
    atom::FocusMonitor,
    atom::MoveWindowToMonitor,
    atom::ReconcileNow,
    atom::RemoveWindowFromAllWorkspaces,
    atom::SetWindowGeometry,
    atom::SetWindowPosition,
    atom::SetWindowSize,
    atom::SetWindowBorderWidth,
    atom::SyncWindowFromConfigureNotify,
    atom::CloseWindow
>;

using CommandComposite = std::variant<
    composite::ToggleWindowFloating,
    composite::MoveFocusedWindowToWorkspace,
    composite::FocusNextWindow,
    composite::FocusPrevWindow,
    composite::ToggleFocusedWindowFloating,
    composite::SwitchWorkspaceLocalIndex,
    composite::AdjustMasterFactor,
    composite::IncMaster,
    composite::Zoom
>;

} // namespace command
