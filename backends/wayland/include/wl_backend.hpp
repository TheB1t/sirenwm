#pragma once

#include <backend/backend.hpp>
#include <backend/input_port.hpp>
#include <backend/keyboard_port.hpp>
#include <backend/monitor_port.hpp>

#include <wl_backend_obj.hpp>
#include <wl_display.hpp>
#include <wl_listener.hpp>
#include <wl_renderer.hpp>
#include <wl_scene_graph.hpp>
#include <wl_surface.hpp>

#include <memory>
#include <unordered_map>
#include <vector>
#include <atomic>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#ifndef SIRENWM_NO_LAYER_SHELL
#  include <wlr/types/wlr_layer_shell_v1.h>
#endif
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <xkbcommon/xkbcommon.h>
}

class Core;
class Runtime;

#ifndef SIRENWM_NO_LAYER_SHELL
// ---------------------------------------------------------------------------
// Per-layer-surface state.
// ---------------------------------------------------------------------------
struct WlLayerSurface {
    wlr_layer_surface_v1*       surface     = nullptr;
    wlr_scene_layer_surface_v1* scene_layer = nullptr;

    WlVoidListener              on_map_;
    WlVoidListener              on_unmap_;
    WlVoidListener              on_destroy_;
    WlVoidListener              on_commit_;
};
#endif

// ---------------------------------------------------------------------------
// Per-output state tracked by the backend.
// ---------------------------------------------------------------------------
struct WlOutput {
    wlr_output*       output       = nullptr;
    wlr_scene_output* scene_output = nullptr;
    int               monitor_idx  = -1;   // Core monitor index (after topology apply)

    WlVoidListener    on_frame_;
    WlVoidListener    on_destroy_;
};

// ---------------------------------------------------------------------------
// Per-keyboard device.
// ---------------------------------------------------------------------------
struct WlKeyboard {
    wlr_input_device* device = nullptr;

    WlListener<wlr_keyboard_key_event> on_key_;
    WlVoidListener                     on_modifiers_;
    WlVoidListener                     on_destroy_;
};

// ---------------------------------------------------------------------------
// WaylandBackend — wlroots 0.15 compositor backend.
// ---------------------------------------------------------------------------
class WaylandBackend final : public Backend {
    public:
        WaylandBackend(Core& core, Runtime& runtime);
        ~WaylandBackend() override;

        // Backend interface
        int                          event_fd() const override;
        void                         pump_events(std::size_t max_events_per_tick) override;
        void                         render_frame() override;
        void                         on_reload_applied() override;
        void                         on_start(Core& core) override;
        void                         shutdown() override;
        void                         prepare_exec_restart() override;

        StartupSnapshot              scan_existing_windows() override;

        bool                         close_window(WindowId win) override;

        backend::InputPort*          input_port()    override;
        backend::MonitorPort*        monitor_port()  override;
        backend::RenderPort*         render_port()   override;
        backend::KeyboardPort*       keyboard_port() override;

        std::string                  window_title(WindowId win) const override;
        uint32_t                     window_pid(WindowId win) const override;

        std::shared_ptr<swm::Window> create_window(WindowId id) override;

        // Domain event reactions
        void on(event::WorkspaceSwitched ev) override;
        void on(event::FocusChanged ev) override;
        void on(event::WindowAssignedToWorkspace ev) override;
        void on(event::WindowAdopted ev) override;

        // Allocate a new unique WindowId for a native Wayland surface.
        WindowId alloc_window_id();

        // Map from WindowId → WlSurface (xdg-toplevel windows).
        WlSurface* wl_surface(WindowId id);

        // Called from WlSurface listeners.
        void handle_surface_map(WlSurface* surf);
        void handle_surface_unmap(WlSurface* surf);
        void handle_surface_destroy(WlSurface* surf);

        wlr_renderer*      renderer()    const { return renderer_.renderer(); }
        wlr_scene*         scene()       const { return scene_.scene(); }
        wlr_output_layout* out_layout()  const { return scene_.output_layout(); }
        wlr_seat*          seat()        const { return seat_; }
        wlr_cursor*        cursor()      const { return cursor_; }
        wlr_scene_tree*    scene_root()  const { return scene_.root(); }

    private:
        Core&    core_;
        Runtime& runtime_;

        // Wayland display (owns wl_display, socket, event loop) — destroyed last
        WlDisplay    display_;
        // wlroots backend, renderer+allocator+compositor, scene+layout
        WlBackendObj backend_obj_;
        WlRenderer   renderer_;
        WlSceneGraph scene_;

        // Still raw — will be wrapped in Phase 4 (seat) and later phases
        wlr_seat*            seat_          = nullptr;
        wlr_cursor*          cursor_        = nullptr;
        wlr_xcursor_manager* xcursor_mgr_   = nullptr;
        wlr_xdg_shell*       xdg_shell_     = nullptr;
#ifndef SIRENWM_NO_LAYER_SHELL
        wlr_layer_shell_v1*      layer_shell_ = nullptr;
#endif
        wlr_data_device_manager* data_dev_mgr_ = nullptr;

        // Port implementations
        std::unique_ptr<backend::MonitorPort>  monitor_port_impl_;
        std::unique_ptr<backend::RenderPort>   render_port_impl_;
        std::unique_ptr<backend::InputPort>    input_port_impl_;
        std::unique_ptr<backend::KeyboardPort> keyboard_port_impl_;

        // Output tracking
        std::vector<std::unique_ptr<WlOutput>> outputs_;

        // Keyboard tracking (one per keyboard device)
        std::vector<std::unique_ptr<WlKeyboard>> keyboards_;

        // Staged surfaces awaiting EnsureWindow dispatch (shared_ptr ownership).
        // Moved to surfaces_ (non-owning raw ptr) by create_window(id).
        std::unordered_map<WindowId, std::shared_ptr<WlSurface>> pending_;

        // Active surfaces: WindowId → WlSurface (non-owning; Core owns the Window)
        std::unordered_map<WindowId, WlSurface*> surfaces_;

        // WindowId allocator for native Wayland surfaces (starts at 1, never 0)
        std::atomic<WindowId> next_id_{ 1 };

#ifndef SIRENWM_NO_LAYER_SHELL
        // Active layer-shell surfaces (compositor owns)
        std::vector<std::unique_ptr<WlLayerSurface>> layer_surfaces_;
#endif

        // Current modifier state (accumulated across all keyboards)
        uint32_t mod_state_ = 0;

        // Last window that received activated=true (for deactivation on focus change).
        WindowId focused_window_ = NO_WINDOW;

        // True while a pointer grab is active (mouse drag/resize).
        // Suppresses pointer focus forwarding to clients.
        bool pointer_grabbed_ = false;

        // True when running on a software (pixman) renderer with no DRM device.
        // In this mode cursor attachment and xcursor upload must be skipped.
        bool software_renderer_ = false;

        // Backend-level signal listeners
        WlListener<wlr_output>       on_new_output_;
        WlListener<wlr_input_device> on_new_input_;
        WlListener<wlr_xdg_surface>  on_new_xdg_surface_;
#ifndef SIRENWM_NO_LAYER_SHELL
        WlListener<wlr_layer_surface_v1> on_new_layer_surface_;
#endif
        WlListener<wlr_pointer_motion_event>          on_cursor_motion_;
        WlListener<wlr_pointer_motion_absolute_event> on_cursor_motion_abs_;
        WlListener<wlr_pointer_button_event>          on_cursor_button_;
        WlListener<wlr_pointer_axis_event>            on_cursor_axis_;
        WlVoidListener                                on_cursor_frame_;
        WlListener<wlr_seat_pointer_request_set_cursor_event>  on_request_cursor_;
        WlListener<wlr_seat_request_set_selection_event>       on_request_set_selection_;

        // Signal handlers
        void handle_new_output(wlr_output* output);
        void handle_new_input(wlr_input_device* device);
        void handle_new_xdg_surface(wlr_xdg_surface* surface);
#ifndef SIRENWM_NO_LAYER_SHELL
        void handle_new_layer_surface(wlr_layer_surface_v1* surface);
        void handle_layer_surface_destroy(WlLayerSurface* ls);
        void arrange_layers(wlr_output* output);
#endif
        void handle_output_frame(WlOutput* out);
        void handle_output_destroy(WlOutput* out);
        void handle_cursor_motion(wlr_pointer_motion_event* ev);
        void handle_cursor_motion_abs(wlr_pointer_motion_absolute_event* ev);
        void handle_cursor_button(wlr_pointer_button_event* ev);
        void handle_cursor_axis(wlr_pointer_axis_event* ev);
        void handle_cursor_frame();
        void handle_request_cursor(wlr_seat_pointer_request_set_cursor_event* ev);
        void handle_request_set_selection(wlr_seat_request_set_selection_event* ev);

        // Keyboard device handlers
        void handle_new_keyboard(wlr_input_device* device);
        void handle_new_pointer(wlr_input_device* device);
        void handle_keyboard_key(WlKeyboard* kb, wlr_keyboard_key_event* ev);
        void handle_keyboard_modifiers(WlKeyboard* kb);
        void handle_keyboard_destroy(WlKeyboard* kb);

        // Internal helpers
        void      apply_core_backend_effects();
        void      process_cursor_motion(uint32_t time_ms);
        WlOutput* output_at(double x, double y);
        void      set_cursor(const char* name);
};
