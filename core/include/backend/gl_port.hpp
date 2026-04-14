#pragma once

// ---------------------------------------------------------------------------
// Backend-agnostic OpenGL window capability interface.
// Feature modules (debug_ui, overlays) consume this instead of EGL/platform
// headers directly.  Backend provides the concrete implementation.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <vector>

#include <support/vec.hpp>

namespace backend {

// Platform-independent input event delivered by a GLWindow.
struct GLInputEvent {
    enum Type { MouseMove, MouseButton, Key, Scroll, Char, Resize, Close };
    Type     type = MouseMove;
    Vec2i    pos;                      // mouse position (or unused for non-mouse events)
    int      button  = 0;             // MouseButton: 0=left, 1=right, 2=middle
    bool     pressed = false;         // MouseButton / Key: true=down, false=up
    uint32_t key     = 0;             // Key: platform-neutral keysym
    uint32_t ch      = 0;             // Char: Unicode codepoint
    float    scroll  = 0.0f;          // Scroll: positive=up, negative=down
    Vec2i    resize;                   // Resize: new dimensions
};

struct GLWindowCreateInfo {
    Vec2i size              = { 800, 600 };
    bool  override_redirect = true;   // skip WM management
    bool  keep_above        = true;   // stay on top
};

class GLWindow {
    public:
        virtual ~GLWindow() = default;

        // File descriptor for select()/poll() — becomes readable when the
        // window has pending events (input, resize, close).
        virtual int fd() const = 0;

        // Drain platform events and return them as portable structs.
        virtual std::vector<GLInputEvent> poll_events() = 0;

        // GL context control — bracket each frame:
        //   make_current() → glViewport + ImGui + swap_buffers()
        virtual void make_current() = 0;
        virtual void swap_buffers() = 0;

        // Visibility.
        virtual void show()          = 0;
        virtual void hide()          = 0;
        virtual bool visible() const = 0;

        // Current dimensions (updated on Resize events).
        virtual int width()  const = 0;
        virtual int height() const = 0;
};

class GLPort {
    public:
        virtual ~GLPort() = default;

        virtual std::unique_ptr<GLWindow>
        create_window(const GLWindowCreateInfo& info = {}) = 0;
};

} // namespace backend
