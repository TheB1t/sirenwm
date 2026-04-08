#pragma once

#include <window_state.hpp>

namespace swm {

// Base window class. Core operates on swm::Window& / shared_ptr<swm::Window>.
// Backends subclass to attach transport-specific state (X11Window, etc.).
// Destroying the object means the window is dead — no alive_ flags needed.
class Window : public WindowState {
    public:
        virtual ~Window() = default;
};

} // namespace swm
