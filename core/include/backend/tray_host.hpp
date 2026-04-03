#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <backend/events.hpp>

namespace backend {

class TrayHost {
    public:
        virtual ~TrayHost()                        = default;

        virtual WindowId window() const            = 0;
        virtual bool     owns_selection() const    = 0;
        virtual int      width() const             = 0;

        virtual void     set_visible(bool visible) = 0;
        virtual void     reposition(WindowId owner_bar_window, int bar_right_x, int bar_y) = 0;
        // Move the tray window to a different bar (called during rebalance).
        // Updates the bar association regardless of current icon count.
        virtual void                  attach_to_bar(WindowId bar_win, int bar_x, int bar_y, int bar_w) = 0;
        virtual void                  raise(WindowId bar_sibling)       = 0;

        virtual bool                  contains_icon(WindowId win) const = 0;
        virtual std::vector<WindowId> icon_windows() const              = 0;
        virtual std::string           icon_wm_class(WindowId win) const = 0;

        // Adopt a specific icon window (e.g. one that returned to root after MANAGER).
        // No-op if the tray does not own the selection or already has the icon.
        virtual void adopt_icon(WindowId win) = 0;
        // Reparent an icon from this tray to another tray container.
        // No selection ownership change occurs — just xcb_reparent_window.
        // Returns true if the icon was found and transferred.
        virtual bool transfer_icon_to(TrayHost& dst, WindowId win) = 0;

        virtual bool handle_client_message(const event::ClientMessageEv& ev,
            WindowId* docked_icon_out)                                   = 0;
        virtual bool handle_destroy_notify(WindowId win)                 = 0;
        virtual bool handle_unmap_notify(WindowId win)                   = 0;
        virtual bool handle_configure_notify(WindowId win)               = 0;
        virtual bool handle_property_notify(WindowId win, uint32_t atom) = 0;
        virtual bool handle_button_event(const event::ButtonEv& ev)      = 0;
};

} // namespace backend