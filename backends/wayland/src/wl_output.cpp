#include <wl_output.hpp>

WlOutput::WlOutput(wlr_output* output, wlr_scene_output* scene_output,
    FrameCb on_frame, DestroyCb on_destroy)
    : output_(output), scene_output_(scene_output) {
    on_frame_.connect(&output->events.frame, [this, cb = std::move(on_frame)](void*) {
            cb(this);
        });
    on_destroy_.connect(&output->events.destroy, [this, cb = std::move(on_destroy)](void*) {
            cb(this);
        });
}

void WlOutput::disconnect() {
    on_frame_.disconnect();
    on_destroy_.disconnect();
}
