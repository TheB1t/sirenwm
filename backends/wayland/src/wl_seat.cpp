#include <wl_seat.hpp>
#include <log.hpp>

WlSeat::WlSeat(wl_display* display, wlr_output_layout* layout, bool software_renderer,
    unsigned int cursor_size)
    : software_(software_renderer) {
    seat_ = wlr_seat_create(display, "seat0");
    if (!seat_)
        LOG_ERR("WlSeat: wlr_seat_create failed");

    cursor_ = wlr_cursor_create();
    if (!cursor_)
        LOG_ERR("WlSeat: wlr_cursor_create failed");

    xcursor_mgr_ = wlr_xcursor_manager_create(nullptr, cursor_size);
    if (!xcursor_mgr_)
        LOG_ERR("WlSeat: wlr_xcursor_manager_create failed");

    wlr_xcursor_manager_load(xcursor_mgr_, 1.0f);

    // In software-renderer mode (no DRM device) cursor attachment is skipped:
    // wlr_output_commit_state triggers wlr_output_cursor_set_buffer which
    // asserts renderer != NULL, but the pixman renderer has no DRM fd.
    if (!software_renderer)
        wlr_cursor_attach_output_layout(cursor_, layout);
}

WlSeat::~WlSeat() {
    if (xcursor_mgr_)
        wlr_xcursor_manager_destroy(xcursor_mgr_);
    if (cursor_)
        wlr_cursor_destroy(cursor_);
    // seat_ is destroyed via wl_display_destroy in WlDisplay dtor.
}

void WlSeat::set_cursor(const char* name) const noexcept {
    // xcursor upload requires a DRM device — skip in software-renderer mode.
    if (software_)
        return;
    wlr_cursor_set_xcursor(cursor_, xcursor_mgr_, name);
}
