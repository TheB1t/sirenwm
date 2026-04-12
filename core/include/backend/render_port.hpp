#pragma once

#include <cstdint>
#include <memory>

#include <backend/events.hpp>
#include <cairo/cairo.h>
#include <vec.hpp>

namespace backend {

struct RenderWindowHints {
    bool override_redirect = true;
    bool dock              = false;
    bool keep_above        = false;
};

struct RenderWindowCreateInfo {
    // Monitor index from Core topology (advisory for backend placement).
    int               monitor_index = -1;
    Vec2i             pos;                          // root-space coordinates (global screen space)
    Vec2i             size                = { 1, 1 };
    uint32_t          background_pixel    = 0;
    bool              want_expose         = false;
    bool              want_button_press   = false;
    bool              want_button_release = false;
    RenderWindowHints hints;
};

class RenderWindow {
    public:
        virtual ~RenderWindow() = default;

        virtual WindowId id() const            = 0;
        virtual int      monitor_index() const = 0;
        virtual int      x() const             = 0;
        virtual int      y() const             = 0;
        virtual int      width() const         = 0;
        virtual int      height() const        = 0;

        // Cairo-first drawing contract:
        // 1. draw through the returned context
        // 2. call present() exactly once per completed frame
        // Backends keep cairo_t ownership; callers must not destroy it.
        virtual cairo_t* cairo()   = 0;
        virtual void     present() = 0;

        virtual void     set_visible(bool visible)                                      = 0;
        virtual void     raise()                                                        = 0;
        virtual void     lower()                                                        = 0;
};

class RenderPort {
    public:
        virtual ~RenderPort() = default;

        // Create a backend-owned utility window for UI modules.
        // Supported consumers: bar, overlays, prompts, other module UI surfaces.
        // Core must stay backend-agnostic and does not operate on RenderWindow.
        virtual std::unique_ptr<RenderWindow>
                         create_window(const RenderWindowCreateInfo& info) = 0;

        virtual uint32_t black_pixel() const = 0;
};

} // namespace backend
