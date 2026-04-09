#include <backend/monitor_port.hpp>
#include <monitor.hpp>
#include <log.hpp>
#include <runtime.hpp>

#include <string>
#include <vector>
#include <cmath>
#include <cstring>

extern "C" {
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/box.h>
#include <wayland-server-protocol.h>
}

namespace backend::wl {

// ---------------------------------------------------------------------------
// WlMonitorPort
// ---------------------------------------------------------------------------
class WlMonitorPort final : public MonitorPort {
    public:
        WlMonitorPort(wlr_output_layout* layout, Runtime& rt)
            : layout_(layout), runtime_(rt) {}

        std::vector<Monitor> get_monitors() override {
            std::vector<Monitor>      result;
            int                       idx = 0;

            wlr_output_layout_output* lo;
            wl_list_for_each(lo, &layout_->outputs, link) {
                wlr_output* o = lo->output;
                if (!o || !o->enabled)
                    continue;

                int ow = 0, oh = 0;
                wlr_output_effective_resolution(o, &ow, &oh);

                wlr_box  box_val{};
                wlr_output_layout_get_box(layout_, o, &box_val);
                wlr_box* box = &box_val;

                Monitor  mon(idx++,
                    o->name ? o->name : "unknown",
                    box ? box->x : 0, box ? box->y : 0,
                    ow, oh);

                result.push_back(std::move(mon));
            }

            return result;
        }

        bool apply_monitor_layout(const std::vector<MonitorLayout>& layouts) override {
            for (const auto& ml : layouts) {
                // Find matching output by name.
                // "default" is a special alias for the first available output.
                wlr_output*               target = nullptr;
                wlr_output_layout_output* lo;
                wl_list_for_each(lo, &layout_->outputs, link) {
                    if (!lo->output) continue;
                    if (ml.output == "default" || ml.output == lo->output->name) {
                        target = lo->output;
                        break;
                    }
                }
                if (!target) {
                    LOG_ERR("WlMonitorPort: output '%s' not found", ml.output.c_str());
                    continue;
                }

                wlr_output_state state;
                wlr_output_state_init(&state);

                if (!ml.enabled) {
                    wlr_output_state_set_enabled(&state, false);
                    if (!wlr_output_commit_state(target, &state))
                        LOG_ERR("WlMonitorPort: failed to disable '%s'", ml.output.c_str());
                    wlr_output_state_finish(&state);
                    continue;
                }

                wlr_output_state_set_enabled(&state, true);

                // Find closest matching mode
                wlr_output_mode* best_mode = nullptr;
                wlr_output_mode* mode;
                wl_list_for_each(mode, &target->modes, link) {
                    if (mode->width == ml.size.x() && mode->height == ml.size.y()) {
                        if (!best_mode) {
                            best_mode = mode;
                        } else if (ml.refresh_rate > 0) {
                            int delta_new  = std::abs(mode->refresh      / 1000 - ml.refresh_rate);
                            int delta_best = std::abs(best_mode->refresh / 1000 - ml.refresh_rate);
                            if (delta_new < delta_best)
                                best_mode = mode;
                        }
                    }
                }

                if (best_mode)
                    wlr_output_state_set_mode(&state, best_mode);
                else
                    LOG_ERR("WlMonitorPort: no %dx%d mode for '%s', using preferred",
                        ml.size.x(), ml.size.y(), ml.output.c_str());

                // Apply rotation
                wl_output_transform xform = WL_OUTPUT_TRANSFORM_NORMAL;
                if (ml.rotation == "left")     xform = WL_OUTPUT_TRANSFORM_90;
                if (ml.rotation == "right")    xform = WL_OUTPUT_TRANSFORM_270;
                if (ml.rotation == "inverted") xform = WL_OUTPUT_TRANSFORM_180;
                wlr_output_state_set_transform(&state, xform);

                if (!wlr_output_commit_state(target, &state))
                    LOG_ERR("WlMonitorPort: failed to commit mode for '%s'", ml.output.c_str());

                wlr_output_state_finish(&state);

                // Position in layout
                wlr_output_layout_add(layout_, target, ml.pos.x(), ml.pos.y());
                LOG_INFO("WlMonitorPort: applied %dx%d+%d+%d to '%s'",
                    ml.size.x(), ml.size.y(), ml.pos.x(), ml.pos.y(), ml.output.c_str());
            }
            return true;
        }

        void select_change_events() override {}
        void flush() override {}

    private:
        wlr_output_layout* layout_;
        Runtime& runtime_;
};

std::unique_ptr<MonitorPort> create_monitor_port(wlr_output_layout* layout, Runtime& rt) {
    return std::make_unique<WlMonitorPort>(layout, rt);
}

} // namespace backend::wl
