#include <wl_backend.hpp>
#include <wl_ports.hpp>

#include <backend/commands.hpp>
#include <backend/events.hpp>
#include <backend/hooks.hpp>
#include <core.hpp>
#include <log.hpp>
#include <runtime.hpp>

#include <config_types.hpp>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

static uint32_t parse_hex_color(const std::string& s) {
    if (s.size() == 7 && s[0] == '#') {
        char* end = nullptr;
        uint32_t rgb = (uint32_t)strtoul(s.c_str() + 1, &end, 16);
        if (end == s.c_str() + 7)
            return 0xFF000000 | rgb;
    }
    return 0xFF888888;
}

// ── Registry ──

void WlBackend::WlRegistry::on_global(uint32_t name, const char* iface, uint32_t version) {
    backend.try_bind(*this, name, iface, version);
}

// ── Backend lifecycle ──

WlBackend::WlBackend(Core&, Runtime& runtime)
    : display_(nullptr)
    , input_(*this)
    , monitor_(*this)
    , render_(*this)
    , keyboard_()
    , runtime_(runtime)
{
    if (!display_) {
        throw std::runtime_error("wl_backend: failed to connect to display server");
    }

    registry_ = std::make_unique<WlRegistry>(display_, *this);
    display_.roundtrip();

    if (!*static_cast<Admin*>(this)) {
        throw std::runtime_error("wl_backend: sirenwm_admin_v1 not available");
    }

    get_surface_list();
    display_.roundtrip();

}

WlBackend::~WlBackend() {
    shutdown();
}

void WlBackend::shutdown() {
    Admin::reset();
    registry_.reset();
    display_ = {};
}

int WlBackend::event_fd() const {
    return display_ ? display_.fd() : -1;
}

void WlBackend::pump_events(std::size_t) {
    if (!display_) return;
    while (display_.prepare_read() != 0)
        display_.dispatch_pending();
    display_.read_events();
    display_.dispatch_pending();
    display_.flush();
}

void WlBackend::render_frame() {
    if (!display_) return;

    auto& core = runtime_.core;
    auto effects = core.take_backend_effects();

    for (const auto& e : effects) {
        switch (e.kind) {
        case BackendEffectKind::MapWindow: {
            auto ws = core.window_state(e.window);
            if (!ws) break;
            configure_surface(e.window,
                              ws->pos().x(), ws->pos().y(),
                              ws->size().x(), ws->size().y());
            set_surface_visible(e.window, 1);
            if (ws->border_width > 0)
                set_surface_border(e.window, ws->border_width, unfocused_border_);
            if (auto flush = core.take_window_flush(e.window))
                (void)flush;
            break;
        }
        case BackendEffectKind::UnmapWindow:
            set_surface_visible(e.window, 0);
            break;
        case BackendEffectKind::FocusWindow:
            if (prev_focused_ != NO_WINDOW && prev_focused_ != e.window)
                set_surface_activated(prev_focused_, 0);
            if (e.window != NO_WINDOW) {
                set_surface_activated(e.window, 1);
                set_surface_border(e.window, 0, 0);
                auto ws = core.window_state(e.window);
                if (ws && ws->border_width > 0)
                    set_surface_border(e.window, ws->border_width, focused_border_);
            }
            if (prev_focused_ != NO_WINDOW && prev_focused_ != e.window) {
                auto prev = core.window_state(prev_focused_);
                if (prev && prev->border_width > 0)
                    set_surface_border(prev_focused_, prev->border_width, unfocused_border_);
            }
            prev_focused_ = e.window;
            break;
        case BackendEffectKind::FocusRoot:
            if (prev_focused_ != NO_WINDOW) {
                set_surface_activated(prev_focused_, 0);
                auto prev = core.window_state(prev_focused_);
                if (prev && prev->border_width > 0)
                    set_surface_border(prev_focused_, prev->border_width, unfocused_border_);
                prev_focused_ = NO_WINDOW;
            }
            break;
        case BackendEffectKind::UpdateWindow: {
            if (e.window == NO_WINDOW) break;
            if (auto flush = core.take_window_flush(e.window)) {
                auto ws = core.window_state(e.window);
                if (ws) {
                    configure_surface(e.window,
                                      ws->pos().x(), ws->pos().y(),
                                      ws->size().x(), ws->size().y());
                    uint32_t color = (e.window == prev_focused_) ? focused_border_ : unfocused_border_;
                    set_surface_border(e.window, ws->border_width, color);
                }
            }
            break;
        }
        case BackendEffectKind::RaiseWindow:
            set_surface_stacking(e.window, 1);
            break;
        case BackendEffectKind::LowerWindow:
            set_surface_stacking(e.window, 0);
            break;
        case BackendEffectKind::CloseWindow:
            close_surface(e.window);
            break;
        case BackendEffectKind::WarpPointer:
            warp_pointer(e.pos.x(), e.pos.y());
            break;
        }
    }

    for (auto win : core.visible_window_ids()) {
        if (auto flush = core.take_window_flush(win)) {
            auto ws = core.window_state(win);
            if (ws)
                configure_surface(win,
                                  ws->pos().x(), ws->pos().y(),
                                  ws->size().x(), ws->size().y());
        }
    }

    display_.flush();
}

void WlBackend::on_start(Core& core) {
    reload_border_colors();
}

void WlBackend::reload_border_colors() {
    auto& theme = runtime_.core.current_settings().theme;
    const auto& fc = theme.border_focused.empty()   ? theme.accent : theme.border_focused;
    const auto& uc = theme.border_unfocused.empty()
        ? (theme.alt_bg.empty() ? theme.bg : theme.alt_bg) : theme.border_unfocused;
    focused_border_   = parse_hex_color(fc);
    unfocused_border_ = parse_hex_color(uc);
}

backend::BackendPorts WlBackend::ports() {
    return { input_, monitor_, render_, keyboard_ };
}

// ── Admin event handlers ──

void WlBackend::on_surface_created(uint32_t id, const char* app_id,
                                    const char* title, uint32_t pid) {
    surfaces_[id] = {id, app_id ? app_id : "", title ? title : "", pid};
}

void WlBackend::on_surface_mapped(uint32_t id) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end()) return;
    it->second.mapped = true;
}

void WlBackend::manage_surface(wl_surface_info& s) {
    if (s.managed) return;
    s.managed = true;
    LOG_INFO("WlBackend: surface %u mapped", s.id);

    auto& core = runtime_.core;
    auto wid = static_cast<WindowId>(s.id);

    core.dispatch(command::atom::EnsureWindow{ .window = wid });

    core.dispatch(command::atom::SetWindowMetadata{
        .window      = wid,
        .wm_instance = s.app_id,
        .wm_class    = s.app_id,
        .title       = s.title,
        .pid         = s.pid,
        .type        = WindowType::Normal,
        .hints       = {},
    });

    core.dispatch(command::atom::SetWindowGeometry{
        .window = wid,
        .pos    = {s.x, s.y},
        .size   = {s.width, s.height},
    });

    runtime_.invoke_hook(hook::WindowRules{ wid });

    core.dispatch(command::atom::MapWindow{ wid });
    runtime_.post_event(event::WindowMapped{ wid });
    core.dispatch(command::atom::ReconcileNow{});

    auto mapped = core.window_state(wid);
    if (mapped && mapped->is_visible())
        core.dispatch(command::atom::FocusWindow{ wid });
}

void WlBackend::on_surface_unmapped(uint32_t id) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end())
        it->second.mapped = false;
}

void WlBackend::on_surface_destroyed(uint32_t id) {
    auto wid = static_cast<WindowId>(id);
    LOG_INFO("WlBackend: surface %u destroyed", id);
    auto& core = runtime_.core;
    bool was_focused = false;
    if (auto focused = core.focused_window_state())
        was_focused = (focused->id == wid);
    bool was_visible = false;
    {
        int ws_id = core.workspace_of_window(wid);
        was_visible = (ws_id >= 0) && core.is_workspace_visible(ws_id);
    }

    runtime_.post_event(event::DestroyNotify{ wid });
    (void)core.dispatch(command::atom::RemoveWindowFromAllWorkspaces{ wid });
    runtime_.post_event(event::WindowUnmapped{ wid, true });

    if (was_visible) {
        (void)core.dispatch(command::atom::ReconcileNow{});
        if (was_focused) {
            if (auto next = core.focused_window_state(); next && next->is_visible())
                (void)core.dispatch(command::atom::FocusWindow{ next->id });
            else
                (void)core.dispatch(command::composite::FocusNextWindow{});
        }
    }

    if (prev_focused_ == wid)
        prev_focused_ = NO_WINDOW;

    surfaces_.erase(id);
}

void WlBackend::on_surface_title_changed(uint32_t id, const char* title) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end())
        it->second.title = title ? title : "";
    runtime_.post_event(event::PropertyNotify{
        static_cast<WindowId>(id), 0 });
}

void WlBackend::on_surface_app_id_changed(uint32_t id, const char* app_id) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end())
        it->second.app_id = app_id ? app_id : "";
}

void WlBackend::on_surface_committed(uint32_t id, int32_t w, int32_t h) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end()) return;
    it->second.width  = w;
    it->second.height = h;

    if (it->second.mapped && !it->second.managed && w > 0 && h > 0)
        manage_surface(it->second);
}

void WlBackend::on_key_press(uint32_t keycode, uint32_t keysym, uint32_t mods) {
    runtime_.post_event(event::KeyPressEv{
        static_cast<uint16_t>(mods),
        static_cast<uint8_t>(keycode),
        keysym});
}

void WlBackend::on_button_press(uint32_t surface_id, int32_t x, int32_t y,
                                 uint32_t button, uint32_t mods, uint32_t released) {
    if (!released && surface_id != 0) {
        auto& core = runtime_.core;
        auto  wid  = static_cast<WindowId>(surface_id);
        if (auto window = core.window_state_any(wid); window && window->is_visible())
            (void)core.dispatch(command::atom::FocusWindow{ wid });
    }

    runtime_.post_event(event::ButtonEv{
        .window    = static_cast<WindowId>(surface_id),
        .root      = static_cast<WindowId>(surface_id),
        .root_pos  = {static_cast<int16_t>(x), static_cast<int16_t>(y)},
        .event_pos = {static_cast<int16_t>(x), static_cast<int16_t>(y)},
        .time      = 0,
        .button    = static_cast<uint8_t>(button - 0x110 + 1),
        .state     = static_cast<uint16_t>(mods),
        .release   = released != 0,
    });
}

void WlBackend::on_pointer_motion(uint32_t surface_id, int32_t x, int32_t y,
                                   uint32_t mods) {
    runtime_.post_event(event::MotionEv{
        static_cast<WindowId>(surface_id),
        {static_cast<int16_t>(x), static_cast<int16_t>(y)},
        static_cast<uint16_t>(mods)});
}

void WlBackend::on_pointer_enter(uint32_t surface_id) {
    auto& core = runtime_.core;
    auto wid = static_cast<WindowId>(surface_id);

    auto window = core.window_state_any(wid);
    if (!window || !window->is_visible()) return;

    core.dispatch(command::atom::FocusWindow{ wid });
}

void WlBackend::on_output_added(uint32_t id, const char* name,
                                 int32_t x, int32_t y, int32_t w, int32_t h,
                                 int32_t refresh) {
    outputs_[id] = {id, name ? name : "", x, y, w, h, refresh};
    runtime_.post_event(event::DisplayTopologyChanged{});
}

void WlBackend::on_output_removed(uint32_t id) {
    outputs_.erase(id);
    runtime_.post_event(event::DisplayTopologyChanged{});
}

void WlBackend::on_overlay_expose(uint32_t overlay_id) {
    runtime_.post_event(event::ExposeWindow{ static_cast<WindowId>(overlay_id) });
}

void WlBackend::on_overlay_button(uint32_t overlay_id, int32_t x, int32_t y,
                                   uint32_t button, uint32_t released) {
    runtime_.post_event(event::ButtonEv{
        .window    = static_cast<WindowId>(overlay_id),
        .root      = static_cast<WindowId>(overlay_id),
        .root_pos  = { static_cast<int16_t>(x), static_cast<int16_t>(y) },
        .event_pos = { static_cast<int16_t>(x), static_cast<int16_t>(y) },
        .time      = 0,
        .button    = static_cast<uint8_t>(button - 0x110 + 1),
        .state     = 0,
        .release   = released != 0,
    });
}
