#include <x11_backend.hpp>

#include <log.hpp>
#include <xconn.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <unistd.h>

namespace {

struct WindowTypeAtoms {
    xcb_atom_t net_wm_window_type = XCB_ATOM_NONE;
    xcb_atom_t dialog             = XCB_ATOM_NONE;
    xcb_atom_t utility            = XCB_ATOM_NONE;
    xcb_atom_t splash             = XCB_ATOM_NONE;
    xcb_atom_t modal              = XCB_ATOM_NONE;
};

struct RestartWinState {
    int  ws_id    = -1;
    bool floating = false;
};

struct RestartState {
    std::unordered_map<xcb_window_t, RestartWinState> windows;
    std::unordered_map<int, int>                      monitor_active_ws; // monitor_idx -> ws_id
    bool                                              had_file = false;
};

struct WindowMetadata {
    std::string wm_instance;
    std::string wm_class;
    bool        wm_type_dialog  = false;
    bool        wm_type_utility = false;
    bool        wm_type_splash  = false;
    bool        wm_type_modal   = false;
    bool        wm_fixed_size   = false;
};

bool has_atom(const std::vector<xcb_atom_t>& atoms, xcb_atom_t needle) {
    if (needle == XCB_ATOM_NONE)
        return false;
    return std::find(atoms.begin(), atoms.end(), needle) != atoms.end();
}

std::string restart_state_path() {
    return "/tmp/sirenwm-restart-state-" + std::to_string((unsigned long)getuid()) + ".txt";
}

RestartState load_restart_state() {
    RestartState  out;
    std::ifstream in(restart_state_path());
    if (!in.is_open())
        return out;

    out.had_file = true;
    std::string token;
    while (in >> token) {
        if (token == "MON") {
            int mon_idx = -1, ws_id = -1;
            if (in >> mon_idx >> ws_id)
                out.monitor_active_ws[mon_idx] = ws_id;
        } else {
            // window line: "<win_id> <ws_id> <floating>"
            xcb_window_t win = (xcb_window_t)std::stoul(token);
            int          ws = -1, fl = 0;
            if (in >> ws >> fl)
                out.windows[win] = RestartWinState{ ws, fl != 0 };
        }
    }
    in.close();
    std::remove(restart_state_path().c_str());
    return out;
}

std::string read_window_title(XConnection& xconn, xcb_window_t win,
    xcb_atom_t net_wm_name, xcb_atom_t utf8_string) {
    auto title = xconn.get_text_property(win, net_wm_name, utf8_string);
    if (title.empty())
        title = xconn.get_text_property(win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING);
    return title;
}

const WindowTypeAtoms& window_type_atoms(XConnection& xconn) {
    static const WindowTypeAtoms atoms = [&xconn]() {
            auto m = xconn.intern_atoms({
                "_NET_WM_WINDOW_TYPE",
                "_NET_WM_WINDOW_TYPE_DIALOG",
                "_NET_WM_WINDOW_TYPE_UTILITY",
                "_NET_WM_WINDOW_TYPE_SPLASH",
                "_NET_WM_WINDOW_TYPE_MODAL",
            });

            WindowTypeAtoms out;
            out.net_wm_window_type = m["_NET_WM_WINDOW_TYPE"];
            out.dialog             = m["_NET_WM_WINDOW_TYPE_DIALOG"];
            out.utility            = m["_NET_WM_WINDOW_TYPE_UTILITY"];
            out.splash             = m["_NET_WM_WINDOW_TYPE_SPLASH"];
            out.modal              = m["_NET_WM_WINDOW_TYPE_MODAL"];
            return out;
        }();
    return atoms;
}

WindowMetadata read_window_metadata(XConnection& xconn, WindowId window) {
    WindowMetadata out;
    auto [instance, cls] = xconn.get_wm_class(window);
    out.wm_instance      = std::move(instance);
    out.wm_class         = std::move(cls);

    const auto& atoms = window_type_atoms(xconn);
    auto        types = xconn.get_atom_list_property(window, atoms.net_wm_window_type);
    out.wm_type_dialog  = has_atom(types, atoms.dialog);
    out.wm_type_utility = has_atom(types, atoms.utility);
    out.wm_type_splash  = has_atom(types, atoms.splash);
    out.wm_type_modal   = has_atom(types, atoms.modal);
    out.wm_fixed_size   = xconn.has_fixed_size_hints(window);
    return out;
}

} // namespace

std::vector<ExistingWindowSnapshot> X11Backend::scan_existing_windows() {
    constexpr int      ICCCM_ICONIC_STATE = 3;
    constexpr uint32_t kManagedEventMask  =
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_FOCUS_CHANGE;

    std::vector<ExistingWindowSnapshot> out;
    if (root_window == NO_WINDOW)
        return out;

    auto atoms = xconn.intern_atoms({
        "_NET_WM_WINDOW_TYPE",
        "_NET_WM_WINDOW_TYPE_DOCK",
        "_NET_WM_WINDOW_TYPE_DESKTOP",
        "_NET_WM_WINDOW_TYPE_NOTIFICATION",
        "_NET_WM_WINDOW_TYPE_TOOLTIP",
        "_NET_WM_WINDOW_TYPE_DND",
        "_NET_WM_NAME",
        "UTF8_STRING",
        "WM_STATE",
    });
    xcb_atom_t NET_WM_WINDOW_TYPE              = atoms["_NET_WM_WINDOW_TYPE"];
    xcb_atom_t NET_WM_WINDOW_TYPE_DOCK         = atoms["_NET_WM_WINDOW_TYPE_DOCK"];
    xcb_atom_t NET_WM_WINDOW_TYPE_DESKTOP      = atoms["_NET_WM_WINDOW_TYPE_DESKTOP"];
    xcb_atom_t NET_WM_WINDOW_TYPE_NOTIFICATION = atoms["_NET_WM_WINDOW_TYPE_NOTIFICATION"];
    xcb_atom_t NET_WM_WINDOW_TYPE_TOOLTIP      = atoms["_NET_WM_WINDOW_TYPE_TOOLTIP"];
    xcb_atom_t NET_WM_WINDOW_TYPE_DND          = atoms["_NET_WM_WINDOW_TYPE_DND"];
    xcb_atom_t NET_WM_NAME       = atoms["_NET_WM_NAME"];
    xcb_atom_t UTF8_STRING       = atoms["UTF8_STRING"];
    xcb_atom_t WM_STATE          = atoms["WM_STATE"];

    auto       children          = xconn.query_tree_children(root_window);
    auto       rstate            = load_restart_state();
    bool       has_restart_state = rstate.had_file;
    auto&      restart_wins      = rstate.windows;
    if (has_restart_state) {
        LOG_INFO("scan_existing_windows: loaded restart snapshot with %d window(s) and %d monitor(s)",
            (int)restart_wins.size(), (int)rstate.monitor_active_ws.size());
        restart_monitor_active_ws = std::move(rstate.monitor_active_ws);
    }

    out.reserve(children.size());
    for (auto win : children) {
        if (win == root_window)
            continue;

        auto it_state     = restart_wins.find(win);
        bool from_restart = (it_state != restart_wins.end());
        if (has_restart_state && !from_restart)
            LOG_DEBUG("adopt: %u not in restart snapshot, using scan filters", win);

        auto attrs = xconn.get_window_attributes(win);
        if (!attrs.valid) {
            LOG_DEBUG("adopt: skip %u (no attributes)", win);
            continue;
        }
        if (attrs.override_redirect) {
            LOG_DEBUG("adopt: skip %u (override-redirect)", win);
            continue;
        }
        if (attrs.win_class != XCB_WINDOW_CLASS_INPUT_OUTPUT) {
            LOG_DEBUG("adopt: skip %u (class=%u)", win, attrs.win_class);
            continue;
        }

        auto [instance, cls] = xconn.get_wm_class(win);
        auto title            = read_window_title(xconn, win, NET_WM_NAME, UTF8_STRING);
        bool identifiable     = !instance.empty() || !cls.empty() || !title.empty();

        int  wm_state         = xconn.get_wm_state_value(win, WM_STATE);
        bool iconic           = (wm_state == ICCCM_ICONIC_STATE);
        auto types            = xconn.get_atom_list_property(win, NET_WM_WINDOW_TYPE);
        bool unsupported_type =
            has_atom(types, NET_WM_WINDOW_TYPE_DOCK) ||
            has_atom(types, NET_WM_WINDOW_TYPE_DESKTOP) ||
            has_atom(types, NET_WM_WINDOW_TYPE_NOTIFICATION) ||
            has_atom(types, NET_WM_WINDOW_TYPE_TOOLTIP) ||
            has_atom(types, NET_WM_WINDOW_TYPE_DND);

        bool default_manage = true;
        if (!from_restart)
            default_manage = (attrs.map_state == XCB_MAP_STATE_VIEWABLE || iconic) &&
                !unsupported_type &&
                identifiable;

        auto meta = read_window_metadata(xconn, win);
        ExistingWindowSnapshot snap{};
        snap.window             = win;
        snap.currently_viewable = (attrs.map_state == XCB_MAP_STATE_VIEWABLE);
        snap.default_manage     = default_manage;
        snap.from_restart       = from_restart;
        snap.event_mask         = kManagedEventMask;
        snap.wm_instance        = std::move(meta.wm_instance);
        snap.wm_class           = std::move(meta.wm_class);
        snap.wm_type_dialog     = meta.wm_type_dialog;
        snap.wm_type_utility    = meta.wm_type_utility;
        snap.wm_type_splash     = meta.wm_type_splash;
        snap.wm_type_modal      = meta.wm_type_modal;
        snap.wm_fixed_size      = meta.wm_fixed_size;
        if (from_restart) {
            snap.restart_workspace_id = it_state->second.ws_id;
            snap.restart_floating     = it_state->second.floating;
        }

        if (auto geo = xconn.get_window_geometry(win)) {
            snap.has_geometry = true;
            snap.geo_x        = geo->x;
            snap.geo_y        = geo->y;
            snap.geo_w        = geo->width;
            snap.geo_h        = geo->height;
        }

        LOG_DEBUG(
            "scan_existing_windows: candidate %u map_state=%u class='%s' instance='%s' source=%s default_manage=%d",
            win, attrs.map_state, cls.c_str(), instance.c_str(),
            from_restart ? "snapshot" : "scan", default_manage ? 1 : 0);
        out.push_back(std::move(snap));
    }

    LOG_INFO("scan_existing_windows: discovered %d candidate window(s)", (int)out.size());
    return out;
}