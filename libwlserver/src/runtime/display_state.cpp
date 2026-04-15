#include <wl/server/runtime/display_state.hpp>

#include <algorithm>

DisplayState::DisplayState(OverlayManager& overlays)
    : overlays_(overlays) {}

void DisplayState::set_command_handler(swm::ipc::BackendCommandHandler* handler) {
    command_handler_ = handler;
}

void DisplayState::set_event_handler(swm::ipc::BackendEventHandler* handler) {
    event_handler_ = handler;
}

uint32_t DisplayState::add_surface(const std::string& app_id, const std::string& title,
    uint32_t pid, uint32_t toplevel_id) {
    uint32_t       id = next_surface_id_++;
    DisplaySurface surface;
    surface.id          = id;
    surface.toplevel_id = toplevel_id;
    surface.app_id      = app_id;
    surface.title       = title;
    surface.pid         = pid;
    auto [it, _]        = surfaces_.emplace(id, std::move(surface));
    swm::ipc::SurfaceCreated msg {};
    msg.id = it->second.id;
    msg.app_id.assign(it->second.app_id.c_str());
    msg.title.assign(it->second.title.c_str());
    msg.pid = it->second.pid;
    emit_event(msg);
    return id;
}

uint32_t DisplayState::surface_id_from_toplevel(uint32_t toplevel_id) const {
    for (const auto& [id, surface] : surfaces_)
        if (surface.toplevel_id == toplevel_id)
            return id;
    return 0;
}

void DisplayState::destroy_surface_for_toplevel(uint32_t toplevel_id) {
    const auto surface_id = surface_id_from_toplevel(toplevel_id);
    if (surface_id != 0)
        surface_destroyed(surface_id);
}

void DisplayState::set_wl_surface_id(uint32_t id, wl::server::SurfaceId sid) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end())
        it->second.wl_surface_id = sid;
}

void DisplayState::surface_mapped(uint32_t id) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end())
        return;
    it->second.mapped                        = true;
    emit_event(swm::ipc::SurfaceMapped { .id = id });
}

void DisplayState::surface_unmapped(uint32_t id) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end())
        return;
    it->second.mapped                          = false;
    emit_event(swm::ipc::SurfaceUnmapped { .id = id });
}

void DisplayState::surface_destroyed(uint32_t id) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end())
        return;
    emit_event(swm::ipc::SurfaceDestroyed { .id = id });
    surfaces_.erase(it);
}

void DisplayState::surface_title_changed(uint32_t id, const std::string& title) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end())
        return;
    it->second.title = title;
    swm::ipc::SurfaceTitleChanged msg {};
    msg.id = id;
    msg.title.assign(title.c_str());
    emit_event(msg);
}

void DisplayState::surface_app_id_changed(uint32_t id, const std::string& app_id) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end())
        return;
    it->second.app_id = app_id;
    swm::ipc::SurfaceAppIdChanged msg {};
    msg.id = id;
    msg.app_id.assign(app_id.c_str());
    emit_event(msg);
}

void DisplayState::surface_committed(uint32_t id, int32_t width, int32_t height) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end())
        return;
    it->second.width  = width;
    it->second.height = height;
    emit_event(swm::ipc::SurfaceCommitted {
        .id     = id,
        .width  = width,
        .height = height,
    });
}

void DisplayState::output_added(uint32_t id, const std::string& name,
    int32_t x, int32_t y, int32_t w, int32_t h, int32_t refresh) {
    outputs_[id] = { id, name, x, y, w, h, refresh };
    swm::ipc::OutputAdded msg {};
    msg.id = id;
    msg.name.assign(name.c_str());
    msg.x       = x;
    msg.y       = y;
    msg.width   = w;
    msg.height  = h;
    msg.refresh = refresh;
    emit_event(msg);
}

void DisplayState::output_removed(uint32_t id) {
    outputs_.erase(id);
    emit_event(swm::ipc::OutputRemoved { .id = id });
}

void DisplayState::key_press(uint32_t keycode, uint32_t keysym, uint32_t mods) {
    emit_event(swm::ipc::Key {
        .keycode = keycode,
        .keysym  = keysym,
        .mods    = mods,
        .pressed = 1,
    });
}

void DisplayState::button_press(uint32_t surface_id, int32_t root_x, int32_t root_y,
    uint32_t button, uint32_t mods, bool released) {
    emit_event(swm::ipc::Button {
        .surface_id = surface_id,
        .x          = root_x,
        .y          = root_y,
        .button     = button,
        .mods       = mods,
        .released   = released ? 1u : 0u,
    });
}

void DisplayState::pointer_motion(uint32_t surface_id, int32_t root_x, int32_t root_y, uint32_t mods) {
    emit_event(swm::ipc::PointerMotion {
        .surface_id = surface_id,
        .x          = root_x,
        .y          = root_y,
        .mods       = mods,
    });
}

void DisplayState::pointer_enter(uint32_t surface_id) {
    emit_event(swm::ipc::PointerEnter { .surface_id = surface_id });
}

bool DisplayState::is_intercepted(uint32_t keysym, uint32_t mods) const {
    constexpr uint32_t kModMask      = 0xFFu;
    constexpr uint32_t kCapsLockMask = 0x02u;
    constexpr uint32_t kNumLockMask  = 0x10u;
    const uint32_t     state         = (mods & kModMask) & ~(kCapsLockMask | kNumLockMask);

    for (const auto& ki : intercepts_) {
        const uint32_t wanted = (ki.mods & kModMask) & ~(kCapsLockMask | kNumLockMask);
        if (ki.keysym == keysym && (state & wanted) == wanted)
            return true;
    }
    return false;
}

const DisplaySurface* DisplayState::surface(uint32_t id) const {
    auto it = surfaces_.find(id);
    return it != surfaces_.end() ? &it->second : nullptr;
}

std::vector<DisplaySurface> DisplayState::snapshot_surfaces() const {
    std::vector<DisplaySurface> result;
    result.reserve(surfaces_.size());
    for (const auto& [_, surface] : surfaces_)
        result.push_back(surface);
    return result;
}

std::vector<DisplayState::OutputInfo> DisplayState::snapshot_outputs() const {
    std::vector<OutputInfo> result;
    result.reserve(outputs_.size());
    for (const auto& [_, output] : outputs_)
        result.push_back(output);
    return result;
}

uint32_t DisplayState::surface_at(int32_t x, int32_t y) const {
    uint32_t best_id       = 0;
    uint32_t best_stacking = 0;
    for (const auto& [id, surface] : surfaces_) {
        if (!surface.mapped || !surface.visible)
            continue;
        const auto bw = static_cast<int32_t>(surface.border_width);
        if (x >= surface.x - bw && x < surface.x + surface.width + bw &&
            y >= surface.y - bw && y < surface.y + surface.height + bw) {
            if (best_id == 0 || surface.stacking >= best_stacking) {
                best_id       = id;
                best_stacking = surface.stacking;
            }
        }
    }
    return best_id;
}

std::vector<const DisplaySurface*> DisplayState::visible_surfaces_by_stacking() const {
    std::vector<const DisplaySurface*> result;
    for (const auto& [_, surface] : surfaces_)
        if (surface.mapped && surface.visible)
            result.push_back(&surface);
    std::sort(result.begin(), result.end(),
        [](const DisplaySurface* lhs, const DisplaySurface* rhs) {
            return lhs->stacking < rhs->stacking;
        });
    return result;
}

void DisplayState::overlay_button(uint32_t overlay_id, int32_t x, int32_t y, uint32_t button, bool released) {
    emit_event(swm::ipc::OverlayButton {
        .overlay_id = overlay_id,
        .x          = x,
        .y          = y,
        .button     = button,
        .released   = released ? 1u : 0u,
    });
}

void DisplayState::configure_surface(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end())
        return;
    it->second.x      = x;
    it->second.y      = y;
    it->second.width  = w;
    it->second.height = h;
    dispatch_command(swm::ipc::ConfigureSurface {
        .surface_id = id,
        .x          = x,
        .y          = y,
        .width      = w,
        .height     = h,
    });
}

void DisplayState::set_surface_activated(uint32_t id, bool activated) {
    dispatch_command(swm::ipc::SetSurfaceActivated {
        .surface_id = id,
        .activated  = activated ? 1u : 0u,
    });
}

void DisplayState::set_surface_visible(uint32_t id, bool visible) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end())
        it->second.visible = visible;
    dispatch_command(swm::ipc::SetSurfaceVisible {
        .surface_id = id,
        .visible    = visible ? 1u : 0u,
    });
}

void DisplayState::set_surface_stacking(uint32_t id, uint32_t mode) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end())
        it->second.stacking = mode;
    dispatch_command(swm::ipc::SetSurfaceStacking {
        .surface_id = id,
        .raised     = mode,
    });
}

void DisplayState::close_surface(uint32_t id) {
    dispatch_command(swm::ipc::CloseSurface { .surface_id = id });
}

void DisplayState::set_keyboard_intercepts(std::span<const swm::ipc::KeyIntercept> keys) {
    intercepts_.assign(keys.begin(), keys.end());
}

void DisplayState::warp_pointer(int32_t x, int32_t y) {
    dispatch_command(swm::ipc::WarpPointer { .x = x, .y = y });
}

void DisplayState::grab_pointer() {
    dispatch_command(swm::ipc::GrabPointer {});
}

void DisplayState::ungrab_pointer() {
    dispatch_command(swm::ipc::UngrabPointer {});
}

void DisplayState::set_surface_border(uint32_t id, uint32_t width, uint32_t color) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end()) {
        it->second.border_width = width;
        it->second.border_color = color;
    }
    dispatch_command(swm::ipc::SetSurfaceBorder {
        .surface_id = id,
        .width      = width,
        .color      = color,
    });
}

void DisplayState::create_overlay(uint32_t overlay_id, int32_t x, int32_t y, int32_t w, int32_t h) {
    overlays_.create(overlay_id, x, y, w, h);
    emit_event(swm::ipc::OverlayExpose { .overlay_id = overlay_id });
}

void DisplayState::update_overlay(uint32_t overlay_id, int32_t fd, uint32_t size) {
    overlays_.update(overlay_id, fd, size);
}

void DisplayState::destroy_overlay(uint32_t overlay_id) {
    overlays_.destroy(overlay_id);
}

void DisplayState::on(const wl::server::XdgToplevelCreated& msg) {
    const auto surface_id = add_surface("", "", msg.pid, msg.toplevel_id);
    set_wl_surface_id(surface_id, msg.surface_id);
}

void DisplayState::on(const wl::server::XdgToplevelDestroyed& msg) {
    destroy_surface_for_toplevel(msg.toplevel_id);
}

void DisplayState::on(const wl::server::XdgToplevelMapped& msg) {
    const auto surface_id = surface_id_from_toplevel(msg.toplevel_id);
    if (surface_id == 0)
        return;
    surface_mapped(surface_id);
    surface_committed(surface_id, msg.width, msg.height);
}

void DisplayState::on(const wl::server::XdgToplevelCommitted& msg) {
    const auto surface_id = surface_id_from_toplevel(msg.toplevel_id);
    if (surface_id == 0)
        return;
    surface_committed(surface_id, msg.width, msg.height);
}

void DisplayState::on(const wl::server::XdgTitleChanged& msg) {
    const auto surface_id = surface_id_from_toplevel(msg.toplevel_id);
    if (surface_id == 0)
        return;
    surface_title_changed(surface_id, msg.title);
}

void DisplayState::on(const wl::server::XdgAppIdChanged& msg) {
    const auto surface_id = surface_id_from_toplevel(msg.toplevel_id);
    if (surface_id == 0)
        return;
    surface_app_id_changed(surface_id, msg.app_id);
}

void DisplayState::on(const wl::server::XdgFullscreenRequested& msg) {
    (void)msg;
}

void DisplayState::on(const wl::server::XdgMinSizeChanged& msg) {
    (void)msg;
}

void DisplayState::on(const wl::server::XdgMaxSizeChanged& msg) {
    (void)msg;
}
