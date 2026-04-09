#pragma once

// ---------------------------------------------------------------------------
// IEventEmitter — abstract interface for managing event receivers.
// Runtime implements this to register/unregister IEventReceiver instances.
// The actual emit<Ev>() dispatch stays as a template on the concrete class
// (templates cannot be virtual).
// ---------------------------------------------------------------------------

class IEventReceiver;

class IEventEmitter {
public:
    virtual ~IEventEmitter() = default;
    virtual void add_receiver(IEventReceiver* receiver)    = 0;
    virtual void remove_receiver(IEventReceiver* receiver) = 0;
};
