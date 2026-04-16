#include <x11_backend.hpp>
#include <x11/x11_atoms.hpp>

#include <support/log.hpp>
#include <runtime/restart_state.hpp>
#include <x11/xconn.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <unistd.h>

namespace {

struct RestartWinState {
    int  ws_id             = -1;
    bool floating          = false;
    bool fullscreen        = false;
    bool hidden_explicitly = false;
    bool borderless        = false;
};

struct RestartState {
    std::unordered_map<xcb_window_t, RestartWinState> windows;
    std::unordered_map<MonitorId, WorkspaceId>        monitor_active_ws; // monitor_idx -> ws_id
    bool had_file = false;
};

struct WindowMetadata {
    std::string wm_instance;
    std::string wm_class;
    WindowType  type              = WindowType::Normal;
    bool        wm_fixed_size     = false;
    bool        wm_no_decorations = false;
};

RestartState load_restart_state() {
    RestartState  out;
    std::ifstream in(restart_state_path());
    if (!in.is_open())
        return out;

    out.had_file = true;
    std::string token;
    while (in >> token) {
        if (token == "MON") {
            int mon_idx_raw = -1, ws_id_raw = -1;
            if (in >> mon_idx_raw >> ws_id_raw)
                out.monitor_active_ws[MonitorId{ mon_idx_raw }] = WorkspaceId{ ws_id_raw };
        } else if (token == "WINDOW") {
            // "WINDOW <win_id> <ws_id> <floating> [<fullscreen> [<hidden_explicitly> [<borderless>]]]"
            // Extra fields are optional so older state files still parse correctly.
            std::string line;
            if (!std::getline(in, line))
                continue;
            std::istringstream ss(line);
            xcb_window_t       win = 0;
            int                ws = -1, fl = 0, fs = 0, he = 0, bl = 0;
            if (!(ss >> win >> ws >> fl))
                continue;
            ss >> fs; ss >> he; ss >> bl; // ignore failures — fields are optional
            out.windows[win] = RestartWinState{ ws, fl != 0, fs != 0, he != 0, bl != 0 };
        }
    }
    in.close();
    std::remove(restart_state_path().c_str());
    LOG_DEBUG("load_restart_state: %d window(s), %d monitor(s)",
        (int)out.windows.size(), (int)out.monitor_active_ws.size());
    return out;
}

std::string read_window_title(XConnection& xconn, xcb_window_t win,
    xcb_atom_t net_wm_name, xcb_atom_t utf8_string) {
    auto title = xconn.get_text_property(win, net_wm_name, utf8_string);
    if (title.empty())
        title = xconn.get_text_property(win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING);
    return title;
}

WindowMetadata read_window_metadata(XConnection& xconn, WindowId window) {
    WindowMetadata out;
    auto [instance, cls] = xconn.get_wm_class(window);
    out.wm_instance      = std::move(instance);
    out.wm_class         = std::move(cls);

    const auto& atoms = window_type_atoms(xconn);
    auto        types = xconn.get_atom_list_property(window, atoms.net_wm_window_type);
    if (has_atom(types, atoms.modal))        out.type = WindowType::Modal;
    else if (has_atom(types, atoms.dialog))  out.type = WindowType::Dialog;
    else if (has_atom(types, atoms.utility)) out.type = WindowType::Utility;
    else if (has_atom(types, atoms.splash))  out.type = WindowType::Splash;
    out.wm_fixed_size     = xconn.has_fixed_size_hints(window);
    out.wm_no_decorations = xconn.motif_no_decorations(window);
    return out;
}

} // namespace

StartupSnapshot X11Backend::scan_existing_windows() {
    constexpr int      ICCCM_ICONIC_STATE = 3;
    constexpr uint32_t kManagedEventMask  =
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_FOCUS_CHANGE |
        XCB_EVENT_MASK_PROPERTY_CHANGE;

    StartupSnapshot result;
    auto&           out = result.windows;
    if (root_window == NO_WINDOW)
        return result;

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
    xcb_atom_t NET_WM_NAME                     = atoms["_NET_WM_NAME"];
    xcb_atom_t UTF8_STRING                     = atoms["UTF8_STRING"];
    xcb_atom_t WM_STATE                        = atoms["WM_STATE"];

    auto       children          = xconn.query_tree_children(root_window);
    auto       rstate            = load_restart_state();
    bool       has_restart_state = rstate.had_file;
    auto&      restart_wins      = rstate.windows;
    if (has_restart_state) {
        LOG_INFO("scan_existing_windows: loaded restart snapshot with %d window(s) and %d monitor(s)",
            (int)restart_wins.size(), (int)rstate.monitor_active_ws.size());
        result.monitor_active_ws = std::move(rstate.monitor_active_ws);
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
        auto title        = read_window_title(xconn, win, NET_WM_NAME, UTF8_STRING);
        bool identifiable = !instance.empty() || !cls.empty() || !title.empty();

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

        auto                   meta = read_window_metadata(xconn, win);
        ExistingWindowSnapshot snap{};
        snap.window               = win;
        snap.currently_viewable   = (attrs.map_state == XCB_MAP_STATE_VIEWABLE);
        snap.default_manage       = default_manage;
        snap.from_restart         = from_restart;
        snap.wm_instance          = std::move(meta.wm_instance);
        snap.wm_class             = std::move(meta.wm_class);
        snap.type                 = meta.type;
        snap.hints.fixed_size     = meta.wm_fixed_size;
        snap.hints.no_decorations = meta.wm_no_decorations;
        if (from_restart) {
            snap.restart_workspace_id      = it_state->second.ws_id;
            snap.restart_floating          = it_state->second.floating;
            snap.restart_fullscreen        = it_state->second.fullscreen;
            snap.restart_hidden_explicitly = it_state->second.hidden_explicitly;
            snap.restart_borderless        = it_state->second.borderless;
        }

        if (snap.default_manage)
            xconn.change_window_attributes(win, XCB_CW_EVENT_MASK, &kManagedEventMask);

        if (auto geo = xconn.get_window_geometry(win)) {
            snap.has_geometry = true;
            snap.geo_pos      = { geo->x, geo->y };
            snap.geo_size     = { geo->width, geo->height };
        }

        LOG_DEBUG(
            "scan_existing_windows: candidate %u map_state=%u class='%s' instance='%s' source=%s default_manage=%d",
            win, attrs.map_state, cls.c_str(), instance.c_str(),
            from_restart ? "snapshot" : "scan", default_manage ? 1 : 0);
        out.push_back(std::move(snap));
    }

    // Resolve native X input focus up to a top-level (direct child of root).
    // Electron/VSCode give input focus to an unmanaged sub-surface; walk the
    // parent chain until we land on a window that's a direct child of root.
    xcb_window_t focus = xconn.get_input_focus();
    if (focus != XCB_WINDOW_NONE && focus != root_window) {
        for (int depth = 0; depth < 8; depth++) {
            bool is_toplevel = false;
            for (auto c : children) {
                if (c == focus) {
                    is_toplevel = true; break;
                }
            }
            if (is_toplevel) {
                result.focused_window = focus;
                break;
            }
            auto parent = xconn.query_parent(focus);
            if (!parent)
                break;
            focus = *parent;
        }
    }

    LOG_INFO("scan_existing_windows: discovered %d candidate window(s), focus=%u",
        (int)out.size(), (unsigned)result.focused_window);
    return result;
}
