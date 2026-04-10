#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
}

// ---------------------------------------------------------------------------
// WlSeat — RAII owner of wlr_seat + wlr_cursor + wlr_xcursor_manager.
//
// Destruction order: xcursor_mgr → cursor → seat (seat is destroyed via
// wl_display_destroy, so we don't call wlr_seat_destroy explicitly).
// ---------------------------------------------------------------------------
class WlSeat {
    public:
        // Create a seat named "seat0", a cursor, and an xcursor manager at the
        // given cursor size.  If software_renderer is true the cursor is not
        // attached to the output layout (xcursor upload requires a DRM device).
        WlSeat(wl_display* display, wlr_output_layout* layout, bool software_renderer,
            unsigned int cursor_size = 24);
        ~WlSeat();

        // Non-copyable, non-movable (listeners hold raw pointers into this object).
        WlSeat(const WlSeat&)            = delete;
        WlSeat& operator=(const WlSeat&) = delete;
        WlSeat(WlSeat&&)                 = delete;
        WlSeat& operator=(WlSeat&&)      = delete;

        wlr_seat*            seat()       const noexcept { return seat_; }
        wlr_cursor*          cursor()     const noexcept { return cursor_; }
        wlr_xcursor_manager* xcursor()    const noexcept { return xcursor_mgr_; }

        // Set the named xcursor shape on the cursor (no-op when software renderer).
        void set_cursor(const char* name) const noexcept;

    private:
        wlr_seat*   seat_                 = nullptr;
        wlr_cursor* cursor_               = nullptr;
        wlr_xcursor_manager* xcursor_mgr_ = nullptr;
        bool software_                    = false;
};
