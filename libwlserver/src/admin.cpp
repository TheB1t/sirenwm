#include <wl/server/admin.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
#include "sirenwm-server-v1-protocol.h"
}

Admin* Admin::self(wl_resource* r) {
    return static_cast<Admin*>(wl_resource_get_user_data(r));
}

const void* Admin::vtable() {
    static const struct sirenwm_admin_v1_interface impl = {
        .get_surface_list        = [](wl_client*, wl_resource* r) { self(r)->req_get_surface_list(); },
        .configure_surface       = [](wl_client*, wl_resource* r, uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h) { self(r)->req_configure_surface(id, x, y, w, h); },
        .set_surface_activated   = [](wl_client*, wl_resource* r, uint32_t id, uint32_t a) { self(r)->req_set_surface_activated(id, a); },
        .set_surface_visible     = [](wl_client*, wl_resource* r, uint32_t id, uint32_t v) { self(r)->req_set_surface_visible(id, v); },
        .set_surface_stacking    = [](wl_client*, wl_resource* r, uint32_t id, uint32_t m) { self(r)->req_set_surface_stacking(id, m); },
        .close_surface           = [](wl_client*, wl_resource* r, uint32_t id) { self(r)->req_close_surface(id); },
        .set_keyboard_intercepts = [](wl_client*, wl_resource* r, wl_array* k) { self(r)->req_set_keyboard_intercepts(k); },
        .warp_pointer            = [](wl_client*, wl_resource* r, int32_t x, int32_t y) { self(r)->req_warp_pointer(x, y); },
        .set_pointer_constraint  = [](wl_client*, wl_resource* r, int32_t x, int32_t y, int32_t w, int32_t h) { self(r)->req_set_pointer_constraint(x, y, w, h); },
        .grab_pointer            = [](wl_client*, wl_resource* r) { self(r)->req_grab_pointer(); },
        .ungrab_pointer          = [](wl_client*, wl_resource* r) { self(r)->req_ungrab_pointer(); },
        .set_surface_border      = [](wl_client*, wl_resource* r, uint32_t id, uint32_t w, uint32_t c) { self(r)->req_set_surface_border(id, w, c); },
        .create_overlay          = [](wl_client*, wl_resource* r, uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h) { self(r)->req_create_overlay(id, x, y, w, h); },
        .update_overlay          = [](wl_client*, wl_resource* r, uint32_t id, int32_t fd, uint32_t sz) { self(r)->req_update_overlay(id, fd, sz); },
        .destroy_overlay         = [](wl_client*, wl_resource* r, uint32_t id) { self(r)->req_destroy_overlay(id); },
    };
    return &impl;
}

void Admin::bind_thunk(wl_client* client, void* data, uint32_t version, uint32_t id) {
    static_cast<Admin*>(data)->bind(client, version, id);
}

void Admin::bind(wl_client* client, uint32_t version, uint32_t id) {
    if (admin_resource_) {
        fprintf(stderr, "sirenwm-wayland(display-server): rejecting second admin bind\n");
        wl_client_post_implementation_error(client, "only one admin client allowed");
        return;
    }
    auto* resource = wl_resource_create(client, &sirenwm_admin_v1_interface,
                                        static_cast<int>(version), id);
    if (!resource) { wl_client_post_no_memory(client); return; }

    wl_resource_set_implementation(resource, vtable(), this,
        [](wl_resource* r) {
            auto* s = self(r);
            if (s && s->admin_resource_ == r) {
                s->admin_resource_ = nullptr;
                fprintf(stderr, "sirenwm-wayland(display-server): admin client disconnected\n");
            }
        });
    admin_resource_ = resource;
    fprintf(stderr, "sirenwm-wayland(display-server): admin client connected\n");
}

void Admin::req_get_surface_list() {
    for (auto& [id, out] : outputs_)
        sirenwm_admin_v1_send_output_added(admin_resource_, out.id, out.name.c_str(),
                                           out.x, out.y, out.w, out.h, out.refresh);
    for (auto& [id, surf] : surfaces_) {
        sirenwm_admin_v1_send_surface_created(admin_resource_, id,
                                              surf.app_id.c_str(), surf.title.c_str(), surf.pid);
        if (surf.mapped)
            sirenwm_admin_v1_send_surface_mapped(admin_resource_, id);
    }
    sirenwm_admin_v1_send_surface_list_done(admin_resource_);
}

void Admin::req_configure_surface(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end()) return;
    it->second.x = x;
    it->second.y = y;
    it->second.width = w;
    it->second.height = h;
    if (listener_) listener_->on_configure(id, x, y, w, h);
}

void Admin::req_set_surface_activated(uint32_t id, uint32_t a) {
    if (listener_) listener_->on_activate(id, a != 0);
}

void Admin::req_set_surface_visible(uint32_t id, uint32_t v) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end()) it->second.visible = (v != 0);
    if (listener_) listener_->on_visibility(id, v != 0);
}

void Admin::req_set_surface_stacking(uint32_t id, uint32_t m) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end()) it->second.stacking = m;
    if (listener_) listener_->on_stacking(id, m);
}

void Admin::req_close_surface(uint32_t id) {
    if (listener_) listener_->on_close(id);
}

void Admin::req_set_keyboard_intercepts(wl_array* keys) {
    intercepts_.clear();
    if (keys && keys->size >= sizeof(KeyIntercept)) {
        size_t count = keys->size / sizeof(KeyIntercept);
        auto* data = static_cast<const KeyIntercept*>(keys->data);
        intercepts_.assign(data, data + count);
    }
}

void Admin::req_warp_pointer(int32_t x, int32_t y) {
    if (listener_) listener_->on_warp_pointer(x, y);
}

void Admin::req_set_pointer_constraint(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (listener_) listener_->on_pointer_constraint(x, y, w, h);
}

void Admin::req_grab_pointer() { if (listener_) listener_->on_grab_pointer(); }
void Admin::req_ungrab_pointer() { if (listener_) listener_->on_ungrab_pointer(); }

void Admin::req_set_surface_border(uint32_t id, uint32_t w, uint32_t c) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end()) {
        it->second.border_width = w;
        it->second.border_color = c;
    }
    if (listener_) listener_->on_border(id, w, c);
}

void Admin::req_create_overlay(uint32_t oid, int32_t x, int32_t y, int32_t w, int32_t h) {
    overlays_.create(oid, x, y, w, h);
    if (admin_resource_)
        sirenwm_admin_v1_send_overlay_expose(admin_resource_, oid);
}

void Admin::req_update_overlay(uint32_t oid, int32_t fd, uint32_t size) {
    overlays_.update(oid, fd, static_cast<uint32_t>(size));
}

void Admin::req_destroy_overlay(uint32_t oid) {
    overlays_.destroy(oid);
}

Admin::Admin(wl::Display& display, OverlayManager& overlays)
    : overlays_(overlays) {
    global_ = wl_global_create(display.raw(), &sirenwm_admin_v1_interface,
                               1, this, &Admin::bind_thunk);
}

Admin::~Admin() {
    if (global_) wl_global_destroy(global_);
}

void Admin::set_listener(AdminListener* listener) { listener_ = listener; }
bool Admin::has_admin() const { return admin_resource_ != nullptr; }

bool Admin::is_intercepted(uint32_t keysym, uint32_t mods) const {
    constexpr uint32_t kModMask      = 0xFFu;
    constexpr uint32_t kCapsLockMask = 0x02u;
    constexpr uint32_t kNumLockMask  = 0x10u;
    const uint32_t     state         = (mods & kModMask) & ~(kCapsLockMask | kNumLockMask);

    for (const auto& ki : intercepts_) {
        uint32_t wanted = (ki.mods & kModMask) & ~(kCapsLockMask | kNumLockMask);
        if (ki.keysym == keysym && (state & wanted) == wanted)
            return true;
    }
    return false;
}

uint32_t Admin::add_surface(const std::string& app_id, const std::string& title,
                            uint32_t pid, uint32_t toplevel_id) {
    uint32_t id = next_surface_id_++;
    AdminSurface s;
    s.id = id;
    s.toplevel_id = toplevel_id;
    s.app_id = app_id;
    s.title = title;
    s.pid = pid;
    surfaces_[id] = std::move(s);
    if (admin_resource_)
        sirenwm_admin_v1_send_surface_created(admin_resource_, id, app_id.c_str(), title.c_str(), pid);
    return id;
}

uint32_t Admin::admin_id_from_toplevel(uint32_t toplevel_id) const {
    for (auto& [id, s] : surfaces_)
        if (s.toplevel_id == toplevel_id) return id;
    return 0;
}

void Admin::surface_destroyed_by_toplevel(uint32_t toplevel_id) {
    auto admin_id = admin_id_from_toplevel(toplevel_id);
    if (admin_id != 0)
        surface_destroyed(admin_id);
}

void Admin::set_surface_wl_id(uint32_t id, wl::server::SurfaceId sid) {
    auto it = surfaces_.find(id);
    if (it != surfaces_.end())
        it->second.wl_surface_id = sid;
}

void Admin::surface_mapped(uint32_t id) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end()) return;
    it->second.mapped = true;
    if (admin_resource_)
        sirenwm_admin_v1_send_surface_mapped(admin_resource_, id);
}

void Admin::surface_unmapped(uint32_t id) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end()) return;
    it->second.mapped = false;
    if (admin_resource_)
        sirenwm_admin_v1_send_surface_unmapped(admin_resource_, id);
}

void Admin::surface_destroyed(uint32_t id) {
    if (admin_resource_)
        sirenwm_admin_v1_send_surface_destroyed(admin_resource_, id);
    surfaces_.erase(id);
}

void Admin::surface_title_changed(uint32_t id, const std::string& title) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end()) return;
    it->second.title = title;
    if (admin_resource_)
        sirenwm_admin_v1_send_surface_title_changed(admin_resource_, id, title.c_str());
}

void Admin::surface_app_id_changed(uint32_t id, const std::string& app_id) {
    auto it = surfaces_.find(id);
    if (it == surfaces_.end()) return;
    it->second.app_id = app_id;
    if (admin_resource_)
        sirenwm_admin_v1_send_surface_app_id_changed(admin_resource_, id, app_id.c_str());
}

void Admin::surface_committed(uint32_t id, int32_t width, int32_t height) {
    if (admin_resource_)
        sirenwm_admin_v1_send_surface_committed(admin_resource_, id, width, height);
}

void Admin::surface_request_fullscreen(uint32_t id, bool enter) {
    if (admin_resource_)
        sirenwm_admin_v1_send_surface_request_fullscreen(admin_resource_, id, enter ? 1 : 0);
}

void Admin::surface_geometry_hint(uint32_t id, int32_t min_w, int32_t min_h,
                                   int32_t max_w, int32_t max_h) {
    if (admin_resource_)
        sirenwm_admin_v1_send_surface_geometry_hint(admin_resource_, id, min_w, min_h, max_w, max_h);
}

void Admin::output_added(uint32_t id, const std::string& name,
                          int32_t x, int32_t y, int32_t w, int32_t h, int32_t refresh) {
    outputs_[id] = {id, name, x, y, w, h, refresh};
    if (admin_resource_)
        sirenwm_admin_v1_send_output_added(admin_resource_, id, name.c_str(), x, y, w, h, refresh);
}

void Admin::output_removed(uint32_t id) {
    outputs_.erase(id);
    if (admin_resource_)
        sirenwm_admin_v1_send_output_removed(admin_resource_, id);
}

// ---------------------------------------------------------------------------
// wl::server::XdgShellListener bridge
// ---------------------------------------------------------------------------

void Admin::on_toplevel_created(uint32_t toplevel_id,
    wl::server::SurfaceId surface, uint32_t pid) {
    auto admin_id = add_surface("", "", pid, toplevel_id);
    set_surface_wl_id(admin_id, surface);
}

void Admin::on_toplevel_destroyed(uint32_t toplevel_id) {
    surface_destroyed_by_toplevel(toplevel_id);
}

void Admin::on_toplevel_mapped(uint32_t toplevel_id, int32_t buf_w, int32_t buf_h) {
    auto admin_id = admin_id_from_toplevel(toplevel_id);
    if (admin_id == 0)
        return;
    surface_mapped(admin_id);
    surface_committed(admin_id, buf_w, buf_h);
}

void Admin::on_toplevel_committed(uint32_t toplevel_id, int32_t buf_w, int32_t buf_h) {
    auto admin_id = admin_id_from_toplevel(toplevel_id);
    if (admin_id == 0)
        return;
    surface_committed(admin_id, buf_w, buf_h);
}

void Admin::on_title_changed(uint32_t toplevel_id, const std::string& title) {
    auto admin_id = admin_id_from_toplevel(toplevel_id);
    if (admin_id == 0)
        return;
    surface_title_changed(admin_id, title);
}

void Admin::on_app_id_changed(uint32_t toplevel_id, const std::string& app_id) {
    auto admin_id = admin_id_from_toplevel(toplevel_id);
    if (admin_id == 0)
        return;
    surface_app_id_changed(admin_id, app_id);
}

void Admin::on_fullscreen_requested(uint32_t toplevel_id, bool enter) {
    auto admin_id = admin_id_from_toplevel(toplevel_id);
    if (admin_id == 0)
        return;
    surface_request_fullscreen(admin_id, enter);
}

void Admin::on_min_size_changed(uint32_t toplevel_id, int32_t w, int32_t h) {
    auto admin_id = admin_id_from_toplevel(toplevel_id);
    if (admin_id == 0)
        return;
    surface_geometry_hint(admin_id, w, h, 0, 0);
}

void Admin::on_max_size_changed(uint32_t toplevel_id, int32_t w, int32_t h) {
    auto admin_id = admin_id_from_toplevel(toplevel_id);
    if (admin_id == 0)
        return;
    surface_geometry_hint(admin_id, 0, 0, w, h);
}

void Admin::key_press(uint32_t keycode, uint32_t keysym, uint32_t mods) {
    if (admin_resource_)
        sirenwm_admin_v1_send_key_press(admin_resource_, keycode, keysym, mods);
}

void Admin::button_press(uint32_t surface_id, int32_t root_x, int32_t root_y,
                          uint32_t button, uint32_t mods, bool released) {
    if (admin_resource_)
        sirenwm_admin_v1_send_button_press(admin_resource_, surface_id,
                                            root_x, root_y, button, mods, released ? 1 : 0);
}

void Admin::pointer_motion(uint32_t surface_id, int32_t root_x, int32_t root_y, uint32_t mods) {
    if (admin_resource_)
        sirenwm_admin_v1_send_pointer_motion(admin_resource_, surface_id, root_x, root_y, mods);
}

void Admin::pointer_enter(uint32_t surface_id) {
    if (admin_resource_)
        sirenwm_admin_v1_send_pointer_enter(admin_resource_, surface_id);
}

const AdminSurface* Admin::surface(uint32_t id) const {
    auto it = surfaces_.find(id);
    return it != surfaces_.end() ? &it->second : nullptr;
}

uint32_t Admin::surface_at(int32_t x, int32_t y) const {
    uint32_t best_id = 0;
    uint32_t best_stacking = 0;
    for (auto& [id, s] : surfaces_) {
        if (!s.mapped || !s.visible) continue;
        auto bw = static_cast<int32_t>(s.border_width);
        if (x >= s.x - bw && x < s.x + s.width + bw &&
            y >= s.y - bw && y < s.y + s.height + bw) {
            if (best_id == 0 || s.stacking >= best_stacking) {
                best_id = id;
                best_stacking = s.stacking;
            }
        }
    }
    return best_id;
}

std::vector<const AdminSurface*> Admin::visible_surfaces_by_stacking() const {
    std::vector<const AdminSurface*> result;
    for (auto& [_, s] : surfaces_)
        if (s.mapped && s.visible) result.push_back(&s);
    std::sort(result.begin(), result.end(),
              [](const AdminSurface* a, const AdminSurface* b) { return a->stacking < b->stacking; });
    return result;
}

void Admin::overlay_button(uint32_t overlay_id, int32_t x, int32_t y, uint32_t button, bool released) {
    if (admin_resource_)
        sirenwm_admin_v1_send_overlay_button(admin_resource_, overlay_id, x, y, button, released ? 1 : 0);
}
