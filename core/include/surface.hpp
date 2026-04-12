#pragma once

// Surface — backend-agnostic drawable rectangle owned by a module.
//
// A Surface is a fixed-geometry cairo-drawable region on screen, created
// through Runtime::create_surface(). It is immutable in position/size for
// its entire lifetime — if the module needs different geometry (monitor
// layout change, new bar config), it destroys the surface and creates a
// new one.
//
// Modules never see the underlying backend window. Only Runtime has access
// to the backend RenderWindow (via friend) and uses it to wire up X11-only
// extras such as the system tray.

#include <memory>

#include <cairo/cairo.h>

#include <backend/events.hpp>
#include <vec.hpp>

namespace backend {
class RenderWindow;
} // namespace backend

class Runtime;

struct SurfaceCreateInfo {
    int   monitor_index = -1;     // advisory; backend places by pos
    Vec2i pos;                    // root-space coordinates
    Vec2i size                = { 1, 1 };
    bool  want_expose         = false;
    bool  want_button_press   = false;
    bool  want_button_release = false;
    bool  dock                = false;  // semantic: dock/bar surface
    bool  keep_above          = false;  // semantic: keep above other windows
};

class Surface {
    public:
        ~Surface();

        Surface(const Surface&)            = delete;
        Surface& operator=(const Surface&) = delete;
        Surface(Surface&&)                 = delete;
        Surface& operator=(Surface&&)      = delete;

        // TODO: replace with Runtime-level event routing that resolves
        // WindowId -> Surface* before emit (ExposeSurface / SurfaceButton).
        // For now modules match X11 events against this id directly.
        WindowId id() const;

        // Identity & geometry — immutable
        int monitor_index() const;
        int x() const;
        int y() const;
        int width() const;
        int height() const;

        // Drawing
        cairo_t* cairo();
        void     present();

        // Stacking & visibility (not geometry — surface never moves)
        void raise();
        void lower();
        void set_visible(bool visible);

    private:
        friend class Runtime;

        Surface(Runtime& runtime, std::unique_ptr<backend::RenderWindow> window);

        backend::RenderWindow*       backend_window()       { return window_.get(); }
        const backend::RenderWindow* backend_window() const { return window_.get(); }

        Runtime&                               runtime_;
        std::unique_ptr<backend::RenderWindow> window_;
};
