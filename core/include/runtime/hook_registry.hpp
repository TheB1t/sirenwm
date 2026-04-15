#pragma once

// ---------------------------------------------------------------------------
// IHookReceiver / HookRegistry — synchronous filter dispatch.
//
// Hooks are the synchronous counterpart to the event queue: invoke_hook()
// walks all registered receivers in registration order and returns once
// every subscriber has had a chance to read and mutate the args. No queue,
// no drain, no reordering.
//
// Dispatch uses virtual overload resolution on IHookReceiver::on_hook(H&),
// same pattern as IEventReceiver. Every hook type gets a default no-op
// overload so concrete subscribers override only the hooks they care about.
// ---------------------------------------------------------------------------

#include <backend/hooks.hpp>
#include <runtime/pointer_registry.hpp>

class IHookReceiver {
    public:
        virtual ~IHookReceiver() = default;

        virtual void on_hook(hook::WindowRules&)        {}
        virtual void on_hook(hook::ShouldManageWindow&) {}
        virtual void on_hook(hook::CloseWindow&)        {}
};

class HookRegistry {
    PointerRegistry<IHookReceiver> receivers_;

    public:
        void add(IHookReceiver* r)    { receivers_.add(r); }
        void remove(IHookReceiver* r) { receivers_.remove(r); }
        void clear()                  { receivers_.clear(); }

        template<typename H>
        void invoke(H& h) {
            for (auto* r : receivers_) r->on_hook(h);
        }
};
