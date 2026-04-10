#pragma once

#include <wl_listener.hpp>

#include <functional>

extern "C" {
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
}

// ---------------------------------------------------------------------------
// WlOutput — owns the per-output wlroots state and its signal listeners.
//
// Created by WaylandBackend::handle_new_output and stored in outputs_ vector.
// The backend passes callbacks so this class can wire signals without needing
// a back-pointer to WaylandBackend.
// ---------------------------------------------------------------------------
class WlOutput {
    public:
        using FrameCb   = std::function<void (WlOutput*)>;
        using DestroyCb = std::function<void (WlOutput*)>;

        WlOutput(wlr_output* output, wlr_scene_output* scene_output,
            FrameCb on_frame, DestroyCb on_destroy);
        ~WlOutput() = default;

        // Non-copyable, non-movable (listeners hold raw this pointer).
        WlOutput(const WlOutput&)            = delete;
        WlOutput& operator=(const WlOutput&) = delete;
        WlOutput(WlOutput&&)                 = delete;
        WlOutput& operator=(WlOutput&&)      = delete;

        wlr_output*       output()       const noexcept { return output_; }
        wlr_scene_output* scene_output() const noexcept { return scene_output_; }

        int  monitor_idx() const noexcept { return monitor_idx_; }
        void set_monitor_idx(int idx) noexcept { monitor_idx_ = idx; }

        void disconnect();

    private:
        wlr_output*       output_       = nullptr;
        wlr_scene_output* scene_output_ = nullptr;
        int monitor_idx_                = -1;

        WlVoidListener on_frame_;
        WlVoidListener on_destroy_;
};
