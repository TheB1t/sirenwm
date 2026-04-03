#pragma once

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/reply.hpp>
#include <monitor.hpp>
#include <log.hpp>

#include <string>
#include <vector>
#include <optional>
#include <cmath>

namespace xcb {

struct Mode {
    xcb_randr_mode_t id;
    uint16_t         width;
    uint16_t         height;
    double           refresh_rate;
};

class Crtc {
    xcb_connection_t* conn_;
    xcb_randr_crtc_t id;

    public:
        Crtc(xcb_connection_t* conn, xcb_randr_crtc_t crtc) : conn_(conn), id(crtc) {}

        xcb_randr_crtc_t raw()   const { return id; }
        bool             valid() const { return id != XCB_NONE; }

        int output_count() const {
            auto r = xcb::reply(xcb_randr_get_crtc_info_reply(conn_,
                    xcb_randr_get_crtc_info(conn_, id, XCB_CURRENT_TIME), nullptr));
            return r ? xcb_randr_get_crtc_info_outputs_length(r.get()) : -1;
        }

        int16_t  x()                const { return _info() ? _info_cached->x        : 0; }
        int16_t  y()                const { return _info() ? _info_cached->y        : 0; }
        xcb_randr_mode_t mode()     const { return _info() ? _info_cached->mode     : XCB_NONE; }
        uint16_t         rotation() const { return _info() ? _info_cached->rotation : XCB_RANDR_ROTATION_ROTATE_0; }

        uint8_t configure_status(int16_t x, int16_t y, xcb_randr_mode_t mode,
            uint16_t rotation, xcb_randr_output_t output) {
            auto r = xcb::reply(xcb_randr_set_crtc_config_reply(conn_,
                    xcb_randr_set_crtc_config(conn_, id, XCB_CURRENT_TIME, XCB_CURRENT_TIME,
                    x, y, mode, rotation, 1, &output),
                    nullptr));
            return r ? r->status : 0xFF;
        }

        bool configure(int16_t x, int16_t y, xcb_randr_mode_t mode,
            uint16_t rotation, xcb_randr_output_t output) {
            return configure_status(x, y, mode, rotation, output) == XCB_RANDR_SET_CONFIG_SUCCESS;
        }

        void disable() {
            xcb_randr_set_crtc_config(conn_, id, XCB_CURRENT_TIME, XCB_CURRENT_TIME,
                0, 0, XCB_NONE, XCB_RANDR_ROTATION_ROTATE_0, 0, nullptr);
        }

    private:
        // Lazy-cached info — fetched at most once per method call chain.
        // Not stored between calls to avoid stale data.
        mutable xcb_randr_get_crtc_info_reply_t* _info_cached = nullptr;
        bool _info() const {
            if (_info_cached) return true;
            _info_cached = xcb_randr_get_crtc_info_reply(conn_,
                    xcb_randr_get_crtc_info(conn_, id, XCB_CURRENT_TIME), nullptr);
            return _info_cached != nullptr;
        }
};

class Output {
    xcb_connection_t* conn_;
    xcb_randr_output_t id;
    xcb_window_t root;
    std::string output_name;

    public:
        Output(xcb_connection_t* conn, xcb_window_t r, xcb_randr_output_t o, std::string n)
            : conn_(conn), id(o), root(r), output_name(std::move(n)) {}

        const std::string& name() const { return output_name; }
        xcb_randr_output_t raw()  const { return id; }
        xcb_window_t       root_window() const { return root; }

        bool connected() const {
            auto r = xcb::reply(xcb_randr_get_output_info_reply(conn_,
                    xcb_randr_get_output_info(conn_, id, XCB_CURRENT_TIME), nullptr));
            return r && r->connection == XCB_RANDR_CONNECTION_CONNECTED;
        }

        Crtc crtc() const {
            auto r = xcb::reply(xcb_randr_get_output_info_reply(conn_,
                    xcb_randr_get_output_info(conn_, id, XCB_CURRENT_TIME), nullptr));
            return Crtc(conn_, r ? r->crtc : XCB_NONE);
        }

        std::vector<xcb_randr_crtc_t> possible_crtcs() const {
            auto r = xcb::reply(xcb_randr_get_output_info_reply(conn_,
                    xcb_randr_get_output_info(conn_, id, XCB_CURRENT_TIME), nullptr));
            if (!r) return {};
            xcb_randr_crtc_t* arr = xcb_randr_get_output_info_crtcs(r.get());
            int               n   = xcb_randr_get_output_info_crtcs_length(r.get());
            return std::vector<xcb_randr_crtc_t>(arr, arr + n);
        }

        std::vector<Mode> modes() const {
            auto res = xcb::reply(xcb_randr_get_screen_resources_current_reply(conn_,
                    xcb_randr_get_screen_resources_current(conn_, root), nullptr));
            auto oi  = xcb::reply(xcb_randr_get_output_info_reply(conn_,
                    xcb_randr_get_output_info(conn_, id, XCB_CURRENT_TIME), nullptr));
            if (!res || !oi) return {};

            xcb_randr_mode_info_t* all = xcb_randr_get_screen_resources_current_modes(res.get());
            int nall = xcb_randr_get_screen_resources_current_modes_length(res.get());
            xcb_randr_mode_t*      oms = xcb_randr_get_output_info_modes(oi.get());
            int               noms     = xcb_randr_get_output_info_modes_length(oi.get());

            std::vector<Mode> result;
            for (int i = 0; i < noms; i++)
                for (int j = 0; j < nall; j++) {
                    if (all[j].id != oms[i]) continue;
                    double rate = (all[j].htotal && all[j].vtotal)
                    ? (double)all[j].dot_clock / ((double)all[j].htotal * all[j].vtotal)
                    : 0.0;
                    result.push_back({ oms[i], all[j].width, all[j].height, rate });
                    break;
                }
            return result;
        }

        std::optional<Mode> find_mode(int want_w, int want_h, int want_rate) const {
            std::optional<Mode> best;
            for (auto& m : modes()) {
                if (want_w > 0 && (m.width != (uint16_t)want_w || m.height != (uint16_t)want_h))
                    continue;
                if (!best) {
                    best = m; continue;
                }
                if (want_rate > 0) {
                    if (std::abs(m.refresh_rate - want_rate) < std::abs(best->refresh_rate - want_rate))
                        best = m;
                } else {
                    if (m.refresh_rate > best->refresh_rate) best = m;
                }
            }
            return best;
        }

        void set_primary() const { xcb_randr_set_output_primary(conn_, root, id); }

        void disable() { crtc().disable(); }
};

class Screen {
    xcb_connection_t* conn_;
    xcb_window_t root;

    public:
        Screen(xcb_connection_t* conn, xcb_window_t r) : conn_(conn), root(r) {}
        xcb_connection_t* connection() const { return conn_; }

        std::vector<Output> outputs() const {
            auto res = xcb::reply(xcb_randr_get_screen_resources_current_reply(conn_,
                    xcb_randr_get_screen_resources_current(conn_, root), nullptr));
            if (!res) return {};

            xcb_randr_output_t* outs = xcb_randr_get_screen_resources_current_outputs(res.get());
            int                 n    = xcb_randr_get_screen_resources_current_outputs_length(res.get());

            std::vector<Output> result;
            for (int i = 0; i < n; i++) {
                auto oi = xcb::reply(xcb_randr_get_output_info_reply(conn_,
                        xcb_randr_get_output_info(conn_, outs[i], XCB_CURRENT_TIME), nullptr));
                if (!oi) continue;
                std::string name(
                    reinterpret_cast<char*>(xcb_randr_get_output_info_name(oi.get())),
                    xcb_randr_get_output_info_name_length(oi.get()));
                result.emplace_back(conn_, root, outs[i], std::move(name));
            }
            return result;
        }

        std::optional<Output> find_output(const std::string& name) const {
            for (auto& o : outputs())
                if (o.name() == name) return o;
            return std::nullopt;
        }

        std::vector<Crtc> free_crtcs() const {
            auto res = xcb::reply(xcb_randr_get_screen_resources_current_reply(conn_,
                    xcb_randr_get_screen_resources_current(conn_, root), nullptr));
            if (!res) return {};

            xcb_randr_crtc_t* crtcs = xcb_randr_get_screen_resources_current_crtcs(res.get());
            int               n     = xcb_randr_get_screen_resources_current_crtcs_length(res.get());

            std::vector<Crtc> result;
            for (int i = 0; i < n; i++) {
                Crtc c(conn_, crtcs[i]);
                if (c.output_count() == 0) result.push_back(c);
            }
            return result;
        }

        std::vector<Monitor> monitors() const {
            auto r = xcb::reply(xcb_randr_get_monitors_reply(conn_,
                    xcb_randr_get_monitors(conn_, root, 1), nullptr));
            if (!r) return {};

            std::vector<Monitor> result;
            auto                 it = xcb_randr_get_monitors_monitors_iterator(r.get());
            int                  id = 0;
            while (it.rem > 0) {
                xcb_randr_monitor_info_t* info = it.data;
                auto        nr                 = xcb::reply(xcb_get_atom_name_reply(conn_,
                        xcb_get_atom_name(conn_, info->name), nullptr));
                std::string name               = nr
                ? std::string(xcb_get_atom_name_name(nr.get()),
                        xcb_get_atom_name_name_length(nr.get()))
                : "monitor" + std::to_string(id);
                result.emplace_back(id++, name, info->x, info->y, info->width, info->height);
                xcb_randr_monitor_info_next(&it);
            }
            return result;
        }
};

// --- Parsers ---

inline uint16_t parse_rotation(const std::string& s) {
    // xrandr convention: "right" = 90° clockwise = ROTATE_90
    //                    "left"  = 90° counter-clockwise = ROTATE_270
    if (s == "right")    return XCB_RANDR_ROTATION_ROTATE_90;
    if (s == "inverted") return XCB_RANDR_ROTATION_ROTATE_180;
    if (s == "left")     return XCB_RANDR_ROTATION_ROTATE_270;
    return XCB_RANDR_ROTATION_ROTATE_0;
}

} // namespace xcb