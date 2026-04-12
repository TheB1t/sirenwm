#pragma once

#include <backend/events.hpp>

// ---------------------------------------------------------------------------
// IEventReceiver — abstract interface for receiving typed WM events.
// Modules, LuaEvents, and any future event consumer inherit from this.
// All handlers default to no-op so concrete classes override only what they need.
// ---------------------------------------------------------------------------

class IEventReceiver {
    public:
        virtual ~IEventReceiver() = default;

        // Fire-and-forget events (default: no-op)
        virtual void on(event::WindowMapped)              {}
        virtual void on(event::WindowUnmapped)            {}
        virtual void on(event::FocusChanged)              {}
        virtual void on(event::WorkspaceSwitched)         {}
        virtual void on(event::ExposeWindow)              {}
        virtual void on(event::ExposeSurface)             {}
        virtual void on(event::RaiseDocks)                {}
        virtual void on(event::DisplayTopologyChanged)    {}
        virtual void on(event::RuntimeStarted)            {}
        virtual void on(event::RuntimeStopping)           {}
        virtual void on(event::ConfigReloaded)            {}
        virtual void on(event::ChildExited)               {}
        virtual void on(event::ButtonEv)                  {}
        virtual void on(event::SurfaceButton)             {}
        virtual void on(event::MotionEv)                  {}
        virtual void on(event::KeyPressEv)                {}
        virtual void on(event::DestroyNotify)             {}
        virtual void on(event::ConfigureNotify)           {}
        virtual void on(event::PropertyNotify)            {}
        virtual void on(event::WindowAssignedToWorkspace) {}
        virtual void on(event::WindowAdopted)             {}
        virtual void on(const event::CustomEvent&)        {}

        // Stoppable (return true = handled, stops further dispatch).
        // ClientMessageEv remains stoppable for X11 EWMH request handling —
        // the backend consumes the message and returns true to prevent
        // further propagation. This is an event, not a hook, because it
        // fans out across multiple receivers and the stop is optional.
        virtual bool on(event::ClientMessageEv)           { return false; }
};
