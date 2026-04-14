#include <backend/monitor_port.hpp>
#include <xcb/randr.hpp>
#include <x11/xconn.hpp>
#include <runtime/runtime.hpp>
#include <support/log.hpp>

#include <unordered_set>
#include <cstdlib>
#include <string>

namespace {

const char* set_config_status_name(uint8_t status) {
    switch (status) {
        case XCB_RANDR_SET_CONFIG_SUCCESS:            return "SUCCESS";
        case XCB_RANDR_SET_CONFIG_INVALID_CONFIG_TIME: return "INVALID_CONFIG_TIME";
        case XCB_RANDR_SET_CONFIG_INVALID_TIME:       return "INVALID_TIME";
        case XCB_RANDR_SET_CONFIG_FAILED:             return "FAILED";
        default:                                      return "UNKNOWN";
    }
}

// Returns candidate CRTCs for the output.
// Order: current CRTC first (if any), then output-compatible CRTCs, then free CRTCs.
std::vector<xcb::Crtc> select_crtcs(xcb::Screen& screen, xcb::Output& output,
    const std::string& output_name) {
    std::vector<xcb::Crtc>        result;
    std::vector<xcb_randr_crtc_t> seen;

    auto                          push_unique = [&](xcb_randr_crtc_t id) {
            if (id == XCB_NONE)
                return;
            for (auto s : seen)
                if (s == id) return;
            seen.push_back(id);
            result.emplace_back(screen.connection(), id);
        };

    auto current = output.crtc();
    push_unique(current.raw());

    for (auto id : output.possible_crtcs())
        push_unique(id);

    for (auto& free : screen.free_crtcs())
        push_unique(free.raw());

    if (result.empty())
        LOG_ERR("RandR: no candidate CRTC for '%s'", output_name.c_str());

    return result;
}

// Resolves the target mode id from explicit monitor dimensions/rate.
xcb_randr_mode_t resolve_mode(xcb::Output& output, const backend::MonitorLayout& layout) {
    auto best = output.find_mode(layout.size.x(), layout.size.y(), layout.refresh_rate);
    if (!best) {
        LOG_ERR("RandR: no mode matching %dx%d@%dHz for '%s'",
            layout.size.x(), layout.size.y(), layout.refresh_rate, layout.output.c_str());
        return XCB_NONE;
    }
    return best->id;
}

bool is_rotated(const std::string& rotation) {
    return rotation == "left" || rotation == "right";
}

// Applies the full output configuration described by layout entry.
bool configure_output(xcb::Screen& screen,
    XConnection& xconn,
    const backend::MonitorLayout& layout,
    std::unordered_set<xcb_randr_crtc_t>& reserved_crtcs) {
    auto output = screen.find_output(layout.output);
    if (!output) {
        LOG_WARN("RandR: output '%s' not found", layout.output.c_str());
        return true;
    }

    if (!layout.enabled) {
        output->disable();
        LOG_INFO("RandR: disabled '%s'", layout.output.c_str());
        return true;
    }

    if (!output->connected()) {
        // Output was physically disconnected — release its CRTC so the
        // screen geometry shrinks and the WM topology reflects reality.
        output->disable();
        LOG_INFO("RandR: '%s' disconnected, CRTC released", layout.output.c_str());
        return true;
    }

    auto crtcs = select_crtcs(screen, *output, layout.output);
    if (crtcs.empty())
        return false;

    auto current_crtc = output->crtc();

    auto mode = resolve_mode(*output, layout);
    if (mode == XCB_NONE)
        return false;

    int16_t  x = (int16_t)layout.pos.x();
    int16_t  y = (int16_t)layout.pos.y();

    uint16_t rotation = xcb::parse_rotation(layout.rotation);

    // Avoid redundant SetCrtcConfig calls: they generate extra RandR events and
    // can cause visible viewport jitter on some drivers.
    bool already_configured =
        current_crtc.valid() &&
        (reserved_crtcs.count(current_crtc.raw()) == 0) &&
        (current_crtc.x() == x) &&
        (current_crtc.y() == y) &&
        (current_crtc.mode() == mode) &&
        (current_crtc.rotation() == rotation);
    if (already_configured) {
        reserved_crtcs.insert(current_crtc.raw());

        // Set primary only when it actually differs; unconditional calls can
        // create RandR event loops on some drivers.
        if (layout.primary) {
            auto primary = xcb::reply(xcb_randr_get_output_primary_reply(
                xconn.raw(), xcb_randr_get_output_primary(xconn.raw(), output->root_window()), nullptr));
            if (!primary || primary->output != output->raw())
                output->set_primary();
        }

        LOG_DEBUG("RandR: '%s' already configured, skipping SetCrtcConfig",
            layout.output.c_str());
        return true;
    }

    bool tried_any = false;
    for (auto& crtc : crtcs) {
        if (!crtc.valid())
            continue;
        if (reserved_crtcs.count(crtc.raw()) != 0)
            continue;
        tried_any = true;

        uint8_t status = crtc.configure_status(x, y, mode, rotation, output->raw());
        if (status != XCB_RANDR_SET_CONFIG_SUCCESS) {
            LOG_WARN("RandR: CRTC %u rejected config for '%s' (status=%u:%s), trying fallback",
                (unsigned)crtc.raw(), layout.output.c_str(),
                (unsigned)status, set_config_status_name(status));
            continue;
        }

        reserved_crtcs.insert(crtc.raw());
        if (layout.primary)
            output->set_primary();

        std::string mode_desc = std::to_string(layout.size.x()) + "x" + std::to_string(layout.size.y());
        LOG_INFO("RandR: configured '%s': %s@%dHz rot=%s pos=%d+%d (crtc=%u)",
            layout.output.c_str(),
            mode_desc.c_str(),
            layout.refresh_rate,
            layout.rotation.c_str(),
            x, y, (unsigned)crtc.raw());
        return true;
    }

    if (!tried_any) {
        LOG_ERR("RandR: no free CRTC left for '%s' (all candidates already reserved)",
            layout.output.c_str());
        return false;
    }

    LOG_ERR("RandR: SetCrtcConfig failed for '%s' on all candidate CRTCs",
        layout.output.c_str());
    return false;
}

// Compute bounding box of all configured outputs and call SetScreenSize.
void expand_screen_size(XConnection& xconn,
    xcb::Screen& screen,
    const std::vector<backend::MonitorLayout>& layouts) {
    int max_x = 0, max_y = 0;

    for (auto& layout : layouts) {
        if (!layout.enabled) continue;
        auto output = screen.find_output(layout.output);
        if (!output || !output->connected()) continue;

        int w = layout.size.x();
        int h = layout.size.y();
        if (w <= 0 || h <= 0) continue;

        bool rotated = is_rotated(layout.rotation);
        int  sw      = rotated ? h : w;
        int  sh      = rotated ? w : h;
        max_x = std::max(max_x, layout.pos.x() + sw);
        max_y = std::max(max_y, layout.pos.y() + sh);
    }

    if (max_x <= 0 || max_y <= 0) return;

    auto geo   = xconn.get_window_geometry(xconn.root_window());
    int  cur_w = geo ? (int)geo->width  : 0;
    int  cur_h = geo ? (int)geo->height : 0;

    if (cur_w == max_x && cur_h == max_y)
        return;

    xcb_randr_set_screen_size(xconn.raw(), xconn.root_window(),
        (uint16_t)max_x, (uint16_t)max_y,
        xconn.screen()->width_in_millimeters,
        xconn.screen()->height_in_millimeters);
    xconn.force_flush();
    LOG_INFO("RandR: screen size set to %dx%d", max_x, max_y);
}

} // anonymous namespace

// X11 implementation of MonitorPort using RandR.
class X11MonitorPort final : public backend::MonitorPort {
    XConnection& xconn;
    Runtime&     runtime;

    public:
        X11MonitorPort(XConnection& xconn, Runtime& runtime)
            : xconn(xconn), runtime(runtime) {}

        std::vector<Monitor> get_monitors() override {
            return xconn.get_monitors();
        }

        bool apply_monitor_layout(const std::vector<backend::MonitorLayout>& layouts) override {
            if (layouts.empty())
                return true;

            xcb_window_t root = xconn.root_window();
            xcb::Screen  screen(xconn.raw(), root);
            expand_screen_size(xconn, screen, layouts);

            bool                                 all_ok = true;
            std::unordered_set<xcb_randr_crtc_t> reserved_crtcs;
            for (auto& layout : layouts)
                all_ok = configure_output(screen, xconn, layout, reserved_crtcs) && all_ok;

            if (!all_ok) {
                LOG_WARN("RandR: retrying monitor apply with full CRTC reset");
                for (auto& layout : layouts) {
                    auto output = screen.find_output(layout.output);
                    if (!output || !output->connected())
                        continue;
                    output->disable();
                }
                xconn.flush();

                xcb::Screen                          retry_screen(xconn.raw(), root);
                expand_screen_size(xconn, retry_screen, layouts);
                std::unordered_set<xcb_randr_crtc_t> retry_reserved_crtcs;
                for (auto& layout : layouts)
                    configure_output(retry_screen, xconn, layout, retry_reserved_crtcs);
            }

            xconn.flush();
            return all_ok;
        }

        void select_change_events() override {
            xcb_window_t root = xconn.root_window();
            xconn.randr_select_input(root,
                XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
            xconn.flush();

            auto* query = xconn.randr_extension_data();
            if (query)
                runtime.set_backend_extension_event_base(query->first_event);
        }

        void flush() override {
            xconn.flush();
        }
};

namespace backend::x11 {

std::unique_ptr<backend::MonitorPort> create_monitor_port(XConnection& xconn, Runtime& runtime) {
    return std::make_unique<X11MonitorPort>(xconn, runtime);
}

} // namespace backend::x11
