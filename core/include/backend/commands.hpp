#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <backend/events.hpp>
#include <monitor.hpp>
#include <window_state.hpp>

namespace command {

// Core write intents (CQRS-lite):
// - reducers mutate authoritative state
// - all runtime writes pass through this command surface

struct FocusWindow {
    WindowId window = NO_WINDOW;
};

struct SwitchWorkspace {
    int                workspace_id = -1;
    std::optional<int> monitor_index; // nullopt => resolve by policy
};

struct MoveWindowToWorkspace {
    WindowId window       = NO_WINDOW;
    int      workspace_id = -1;
};

struct MoveFocusedWindowToWorkspace {
    int workspace_id = -1;
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
    WindowId window       = NO_WINDOW;
    int      workspace_id = -1;
};

struct AssignWindowWorkspace {
    WindowId window       = NO_WINDOW;
    int      workspace_id = -1;
};

struct SetWindowMetadata {
    WindowId     window = NO_WINDOW;
    std::string  wm_instance;
    std::string  wm_class;
    WindowType   type              = WindowType::Normal;
    bool         wm_fixed_size     = false;
    bool         wm_never_focus    = false;
    bool         preserve_position  = false; // X11: WM_NORMAL_HINTS StaticGravity
    bool         wm_no_decorations = false;
    // Geometry facts: backend supplies these at map time; core classifies from them.
    bool         covers_monitor       = false; // outer size >= monitor usable area
    bool         pre_fullscreen_state = false; // had _NET_WM_STATE_FULLSCREEN before MapRequest
    bool         is_xembed            = false; // _XEMBED_INFO present — not self-managed
    WindowId     transient_for        = NO_WINDOW;
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

struct ToggleWindowFloating {
    WindowId window = NO_WINDOW;
};

struct FocusNextWindow {};
struct FocusPrevWindow {};
struct ToggleFocusedWindowFloating {};
struct SwitchWorkspaceLocalIndex {
    int local_index = -1;
};
struct HideWindow {
    WindowId window = NO_WINDOW;
};
struct ApplyMonitorTopology {
    std::vector<Monitor> monitors;
};
struct ApplyMonitorTopInset {
    int inset_px = 0;
};
struct ApplyMonitorBottomInset {
    int inset_px = 0;
};

struct SetLayout {
    std::string name;
};
struct SetMasterFactor {
    float value = 0.0f;
};
struct AdjustMasterFactor {
    float delta = 0.0f;
};
struct IncMaster {
    int delta = 0;
};
struct FocusMonitor {
    int monitor_index = -1;
};
struct MoveWindowToMonitor {
    WindowId window        = NO_WINDOW;
    int      monitor_index = -1;
};
struct Zoom {};
struct ReconcileNow {};
struct RemoveWindowFromAllWorkspaces {
    WindowId window = NO_WINDOW;
};

struct SetWindowGeometry {
    WindowId window = NO_WINDOW;
    int32_t  x      = 0;
    int32_t  y      = 0;
    uint32_t width  = 0;
    uint32_t height = 0;
};

struct SetWindowPosition {
    WindowId window = NO_WINDOW;
    int32_t  x      = 0;
    int32_t  y      = 0;
};

struct SetWindowSize {
    WindowId window = NO_WINDOW;
    uint32_t width  = 0;
    uint32_t height = 0;
};

struct SetWindowBorderWidth {
    WindowId window       = NO_WINDOW;
    uint32_t border_width = 0;
};

struct SyncWindowFromConfigureNotify {
    WindowId window       = NO_WINDOW;
    int32_t  x            = 0;
    int32_t  y            = 0;
    uint32_t width        = 0;
    uint32_t height       = 0;
    uint32_t border_width = 0;
};

using CoreCommand = std::variant<
    FocusWindow,
    SwitchWorkspace,
    MoveWindowToWorkspace,
    MoveFocusedWindowToWorkspace,
    MapWindow,
    UnmapWindow,
    SetWindowFullscreen,
    EnsureWindow,
    AssignWindowWorkspace,
    SetWindowMetadata,
    SetWindowMapped,
    SetWindowHiddenByWorkspace,
    SetWindowSuppressFocusOnce,
    SetWindowFloating,
    SetWindowBorderless,
    ToggleWindowFloating,
    FocusNextWindow,
    FocusPrevWindow,
    ToggleFocusedWindowFloating,
    SwitchWorkspaceLocalIndex,
    HideWindow,
    ApplyMonitorTopology,
    ApplyMonitorTopInset,
    ApplyMonitorBottomInset,
    SetLayout,
    SetMasterFactor,
    AdjustMasterFactor,
    IncMaster,
    FocusMonitor,
    MoveWindowToMonitor,
    Zoom,
    ReconcileNow,
    RemoveWindowFromAllWorkspaces,
    SetWindowGeometry,
    SetWindowPosition,
    SetWindowSize,
    SetWindowBorderWidth,
    SyncWindowFromConfigureNotify
>;

} // namespace command