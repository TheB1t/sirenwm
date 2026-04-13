#include <wl/server/xwm.hpp>

#include <cstdio>
#include <cstring>

extern "C" {
#include <xcb/composite.h>
}

XWindowManager::XWindowManager(int wm_fd, wl_client* xwl_client,
                               XWaylandShell& shell,
                               wl::server::Compositor& compositor,
                               XwmSurfaceSink& sink,
                               int output_w, int output_h)
    : xwl_client_(xwl_client), shell_(shell)
    , compositor_(compositor), sink_(sink)
    , output_w_(output_w), output_h_(output_h) {
    conn_ = xcb_connect_to_fd(wm_fd, nullptr);
    if (!*this) {
        fprintf(stderr, "xwm: failed to connect to Xwayland\n");
        conn_ = nullptr;
        return;
    }
    owns_connection_ = true;
    screen_ = first_screen(conn_);

    intern_atoms();
    setup_wm();

    flush();
    fprintf(stderr, "xwm: X11 window manager ready\n");
}

XWindowManager::~XWindowManager() {
    if (conn_ && wm_window_)
        destroy_window(wm_window_);
}

void XWindowManager::intern_atoms() {
    auto map = xcb::intern_batch(raw(), atom_names_, ATOM_COUNT);
    for (int i = 0; i < ATOM_COUNT; i++) {
        auto it = map.find(atom_names_[i]);
        atoms_[i] = (it != map.end()) ? it->second : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
    }
}

void XWindowManager::setup_wm() {
    uint32_t mask = XCB_CW_EVENT_MASK;
    uint32_t values[] = {
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
        XCB_EVENT_MASK_PROPERTY_CHANGE
    };
    change_window_attributes(root(), mask, values);

    xcb_composite_redirect_subwindows(raw(), root(),
                                       XCB_COMPOSITE_REDIRECT_MANUAL);

    create_wm_window();

    change_property(root(), atoms_[NET_SUPPORTED], XCB_ATOM_ATOM,
                    32, 1, &atoms_[NET_ACTIVE_WINDOW]);

    set_selection_owner(wm_window_, atoms_[WM_S0]);
    set_selection_owner(wm_window_, atoms_[NET_WM_CM_S0]);
}

void XWindowManager::create_wm_window() {
    wm_window_ = generate_id();
    create_window(wm_window_, root(), 0, 0, 10, 10,
                  XCB_WINDOW_CLASS_INPUT_OUTPUT);

    change_property(wm_window_, atoms_[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW,
                    32, 1, &wm_window_);

    const char* name = "sirenwm";
    change_property(wm_window_, atoms_[NET_WM_NAME], atoms_[UTF8_STRING],
                    8, static_cast<uint32_t>(strlen(name)), name);

    change_property(root(), atoms_[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW,
                    32, 1, &wm_window_);
}

void XWindowManager::dispatch() {
    if (!conn_) return;
    xcb_generic_event_t* ev;
    while ((ev = poll_event())) {
        uint8_t type = ev->response_type & 0x7f;
        switch (type) {
        case XCB_CREATE_NOTIFY:
            handle_create_notify(reinterpret_cast<xcb_create_notify_event_t*>(ev));
            break;
        case XCB_MAP_REQUEST:
            handle_map_request(reinterpret_cast<xcb_map_request_event_t*>(ev));
            break;
        case XCB_CONFIGURE_REQUEST:
            handle_configure_request(reinterpret_cast<xcb_configure_request_event_t*>(ev));
            break;
        case XCB_UNMAP_NOTIFY:
            handle_unmap_notify(reinterpret_cast<xcb_unmap_notify_event_t*>(ev));
            break;
        case XCB_DESTROY_NOTIFY:
            handle_destroy_notify(reinterpret_cast<xcb_destroy_notify_event_t*>(ev));
            break;
        case XCB_PROPERTY_NOTIFY:
            handle_property_notify(reinterpret_cast<xcb_property_notify_event_t*>(ev));
            break;
        case XCB_CLIENT_MESSAGE:
            handle_client_message(reinterpret_cast<xcb_client_message_event_t*>(ev));
            break;
        default:
            break;
        }
        free(ev);
    }
    flush();
}

void XWindowManager::handle_create_notify(xcb_create_notify_event_t* ev) {
    if (ev->window == wm_window_) return;

    XWindow xwin;
    xwin.xcb_id = ev->window;
    xwin.override_redirect = ev->override_redirect;
    xwin.x = ev->x;
    xwin.y = ev->y;
    xwin.width = ev->width;
    xwin.height = ev->height;
    windows_[ev->window] = xwin;

    uint32_t mask = XCB_CW_EVENT_MASK;
    uint32_t values[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
    change_window_attributes(ev->window, mask, values);
}

void XWindowManager::handle_map_request(xcb_map_request_event_t* ev) {
    auto it = windows_.find(ev->window);
    if (it == windows_.end()) return;
    auto& xwin = it->second;

    if (xwin.override_redirect) {
        map_window(ev->window);
        return;
    }

    read_title(xwin);
    read_class(xwin);
    read_pid(xwin);

    map_window(ev->window);
    set_wm_state(ev->window, 1);

    xwin.mapped = true;

    if (xwin.surface_id) {
        xwin.admin_id = sink_.add_surface(xwin.wm_class, xwin.title, xwin.pid);
        admin_ids_.insert(xwin.admin_id);
        sink_.surface_mapped(xwin.admin_id);
        sink_.surface_committed(xwin.admin_id, xwin.width, xwin.height);
    }
}

void XWindowManager::handle_configure_request(xcb_configure_request_event_t* ev) {
    uint32_t mask = 0;
    uint32_t values[7];
    int i = 0;

    if (ev->value_mask & XCB_CONFIG_WINDOW_X) { mask |= XCB_CONFIG_WINDOW_X; values[i++] = static_cast<uint32_t>(ev->x); }
    if (ev->value_mask & XCB_CONFIG_WINDOW_Y) { mask |= XCB_CONFIG_WINDOW_Y; values[i++] = static_cast<uint32_t>(ev->y); }
    if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) { mask |= XCB_CONFIG_WINDOW_WIDTH; values[i++] = ev->width; }
    if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) { mask |= XCB_CONFIG_WINDOW_HEIGHT; values[i++] = ev->height; }
    if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) { mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH; values[i++] = ev->border_width; }
    if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING) { mask |= XCB_CONFIG_WINDOW_SIBLING; values[i++] = ev->sibling; }
    if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) { mask |= XCB_CONFIG_WINDOW_STACK_MODE; values[i++] = ev->stack_mode; }

    configure_window(ev->window, static_cast<uint16_t>(mask), values);
}

void XWindowManager::handle_unmap_notify(xcb_unmap_notify_event_t* ev) {
    auto it = windows_.find(ev->window);
    if (it == windows_.end()) return;
    auto& xwin = it->second;

    if (!xwin.mapped) return;
    xwin.mapped = false;

    if (xwin.admin_id != 0) {
        sink_.surface_destroyed(xwin.admin_id);
        admin_ids_.erase(xwin.admin_id);
        xwin.admin_id = 0;
    }

    set_wm_state(ev->window, 0);
}

void XWindowManager::handle_destroy_notify(xcb_destroy_notify_event_t* ev) {
    auto it = windows_.find(ev->window);
    if (it == windows_.end()) return;

    if (it->second.admin_id != 0) {
        sink_.surface_destroyed(it->second.admin_id);
        admin_ids_.erase(it->second.admin_id);
    }

    windows_.erase(it);
}

void XWindowManager::handle_property_notify(xcb_property_notify_event_t* ev) {
    auto it = windows_.find(ev->window);
    if (it == windows_.end()) return;
    auto& xwin = it->second;

    if (ev->atom == atoms_[NET_WM_NAME] || ev->atom == XCB_ATOM_WM_NAME) {
        read_title(xwin);
        if (xwin.admin_id != 0)
            sink_.surface_title_changed(xwin.admin_id, xwin.title);
    } else if (ev->atom == atoms_[WM_CLASS_ATOM]) {
        read_class(xwin);
        if (xwin.admin_id != 0)
            sink_.surface_app_id_changed(xwin.admin_id, xwin.wm_class);
    }
}

void XWindowManager::handle_client_message(xcb_client_message_event_t* ev) {
    auto it = windows_.find(ev->window);
    if (it == windows_.end()) return;
    auto& xwin = it->second;

    if (ev->type == atoms_[WL_SURFACE_SERIAL]) {
        uint64_t serial = (static_cast<uint64_t>(ev->data.data32[1]) << 32)
                        | ev->data.data32[0];
        xwin.serial = serial;
        try_associate(xwin);
    } else if (ev->type == atoms_[WL_SURFACE_ID]) {
        uint32_t resource_id = ev->data.data32[0];
        auto* resource = wl_client_get_object(xwl_client_, resource_id);
        if (resource) {
            auto sid = compositor_.id_from_resource(resource);
            if (sid)
                associate(xwin, sid);
        }
    }
}

void XWindowManager::try_associate(XWindow& xwin) {
    if (xwin.surface_id) return;
    if (xwin.serial == 0) return;

    auto* wl_surface = shell_.surface_from_serial(xwin.serial);
    if (!wl_surface) return;

    auto sid = compositor_.id_from_resource(wl_surface);
    if (sid)
        associate(xwin, sid);
}

void XWindowManager::associate(XWindow& xwin, wl::server::SurfaceId sid) {
    xwin.surface_id = sid;

    if (xwin.mapped && xwin.admin_id == 0) {
        xwin.admin_id = sink_.add_surface(xwin.wm_class, xwin.title, xwin.pid);
        admin_ids_.insert(xwin.admin_id);
        sink_.set_surface_wl_id(xwin.admin_id, sid);
        sink_.surface_mapped(xwin.admin_id);
        sink_.surface_committed(xwin.admin_id, xwin.width, xwin.height);
    }
}

void XWindowManager::read_title(XWindow& xwin) {
    auto title = xcb::get_text_property(raw(), xwin.xcb_id, atoms_[NET_WM_NAME], atoms_[UTF8_STRING]);
    if (title.empty())
        title = xcb::get_text_property(raw(), xwin.xcb_id, XCB_ATOM_WM_NAME, XCB_ATOM_STRING);
    if (!title.empty())
        xwin.title = std::move(title);
}

void XWindowManager::read_class(XWindow& xwin) {
    auto [instance, cls] = xcb::get_wm_class(raw(), xwin.xcb_id);
    if (!cls.empty())
        xwin.wm_class = std::move(cls);
    else if (!instance.empty())
        xwin.wm_class = std::move(instance);
}

void XWindowManager::read_pid(XWindow& xwin) {
    uint32_t pid = xcb::get_cardinal_property(raw(), xwin.xcb_id, atoms_[NET_WM_PID]);
    if (pid != 0)
        xwin.pid = pid;
}

XWindow* XWindowManager::window_by_admin_id(uint32_t admin_id) {
    for (auto& [_, xwin] : windows_)
        if (xwin.admin_id == admin_id) return &xwin;
    return nullptr;
}

void XWindowManager::configure(uint32_t admin_id, int32_t x, int32_t y,
                                int32_t w, int32_t h) {
    auto* xwin = window_by_admin_id(admin_id);
    if (!xwin || !conn_) return;
    send_configure(xwin->xcb_id, static_cast<int16_t>(x), static_cast<int16_t>(y),
                   static_cast<uint16_t>(w), static_cast<uint16_t>(h));
    flush();
}

void XWindowManager::close(uint32_t admin_id) {
    auto* xwin = window_by_admin_id(admin_id);
    if (!xwin || !conn_) return;
    send_wm_delete(xwin->xcb_id);
    flush();
}

void XWindowManager::activate(uint32_t admin_id, bool activated) {
    if (!activated || !conn_) return;
    auto* xwin = window_by_admin_id(admin_id);
    if (!xwin) return;
    set_net_active(xwin->xcb_id);
    focus_window(xwin->xcb_id);
    flush();
}

bool XWindowManager::owns(uint32_t admin_id) const {
    return admin_ids_.count(admin_id) > 0;
}

void XWindowManager::send_configure(uint32_t win, int16_t x, int16_t y,
                                     uint16_t w, uint16_t h) {
    uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                    XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    uint32_t values[] = {
        static_cast<uint32_t>(x), static_cast<uint32_t>(y),
        static_cast<uint32_t>(w), static_cast<uint32_t>(h)
    };
    configure_window(win, static_cast<uint16_t>(mask), values);
}

void XWindowManager::send_wm_delete(uint32_t win) {
    auto atoms_list = xcb::get_atom_list_property(raw(), win, atoms_[WM_PROTOCOLS]);
    bool supports_delete = false;
    for (auto a : atoms_list) {
        if (a == atoms_[WM_DELETE_WINDOW]) {
            supports_delete = true;
            break;
        }
    }

    if (supports_delete) {
        xcb_client_message_event_t msg = {};
        msg.response_type = XCB_CLIENT_MESSAGE;
        msg.window = win;
        msg.type = atoms_[WM_PROTOCOLS];
        msg.format = 32;
        msg.data.data32[0] = atoms_[WM_DELETE_WINDOW];
        msg.data.data32[1] = XCB_CURRENT_TIME;
        send_event(win, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char*>(&msg));
    } else {
        kill_client(win);
    }
}

void XWindowManager::set_wm_state(uint32_t win, uint32_t state) {
    uint32_t data[2] = { state, XCB_ATOM_NONE };
    change_property(win, atoms_[WM_STATE], atoms_[WM_STATE], 32, 2, data);
}

void XWindowManager::set_net_active(uint32_t win) {
    change_property(root(), atoms_[NET_ACTIVE_WINDOW], XCB_ATOM_WINDOW, 32, 1, &win);
}
