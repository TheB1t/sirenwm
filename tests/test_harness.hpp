#pragma once
// Test harness — assembles Runtime + FakeBackend
// without touching X11 or reading files from disk.
//
// Usage:
//   TestHarness h;                          // single 1920x1080 monitor
//   TestHarness h({ make_monitor(...) });   // custom topology
//   h.use<RulesModule>();                   // load specific modules
//   h.start();                              // call on_start on all modules
//   h.core.dispatch(command::atom::MapWindow{…}); // drive the state machine

#include <core.hpp>
#include <event_receiver.hpp>
#include <runtime.hpp>

#include <variant>
#include <vector>

#include "fake_backend.hpp"

// Test-only shim: old tests inspected a list of core-emitted domain events via
// `core.take_core_events()`. The unified event queue replaced that pipeline, so
// we now record the same events through an IEventReceiver that TestHarness
// subscribes to. The variant type stays so existing test assertions keep
// compiling — just move them from `core.take_core_events()` to
// `h.take_core_events()`.
using CoreDomainEvent = std::variant<
    event::FocusChanged,
    event::WorkspaceSwitched,
    event::RaiseDocks,
    event::DisplayTopologyChanged,
    event::WindowAssignedToWorkspace
>;

class CoreDomainEventRecorder : public IEventReceiver {
    public:
        std::vector<CoreDomainEvent> events;

        void on(event::FocusChanged ev)              override { events.emplace_back(ev); }
        void on(event::WorkspaceSwitched ev)         override { events.emplace_back(ev); }
        void on(event::RaiseDocks ev)                override { events.emplace_back(ev); }
        void on(event::DisplayTopologyChanged ev)    override { events.emplace_back(ev); }
        void on(event::WindowAssignedToWorkspace ev) override { events.emplace_back(ev); }
};

struct TestHarness {
    Runtime                 runtime;
    FakeBackend&            backend;
    CoreDomainEventRecorder recorder;

    // Shorthand so test code can say h.core.dispatch(...).
    Core& core;

    explicit TestHarness(std::vector<Monitor> monitors = {})
        : runtime([mons = monitors.empty()
                ? std::vector<Monitor>{ make_monitor(0, 0, 0, 1920, 1080, "primary") }
                : std::move(monitors)](Core&, Runtime&) {
                return std::make_unique<FakeBackend>(mons);
            })
          , backend(static_cast<FakeBackend&>(runtime.backend()))
          , core(runtime.core)
    {
        runtime.lua.init();
        runtime.add_receiver(&recorder);
        // Init core with fake monitors so tests can dispatch commands
        // before calling start().
        auto mons = backend.monitor_port.get_monitors();
        core.init(std::move(mons));
    }

    // Register a module (same as runtime.use<T>()).
    template<typename T, typename... Args>
    TestHarness& use(Args&&... args) {
        runtime.use<T>(std::forward<Args>(args)...);
        return *this;
    }

    // Call on_start on all registered modules and apply minimal CoreSettings.
    void start() {
        CoreSettings s;
        s.workspace_defs = {{ "[1]", "" }, { "[2]", "" }, { "[3]", "" }};
        core.apply_settings(s);
        runtime.mark_configured();  // bypass load_config() for test harness
        runtime.start();
    }

    // Simulate a window being mapped: create it in core and return its id.
    WindowId map_window(WindowId id, int ws = 0) {
        core.dispatch(command::atom::EnsureWindow{ id, ws });
        core.dispatch(command::atom::SetWindowMapped{ id, true });
        return id;
    }

    // Emit an event through the unified queue (same as runtime.post_event()).
    // drain_events() runs immediately so synchronous test assertions see the
    // module reactions on the next line.
    template<typename Ev>
    void emit(Ev ev) {
        runtime.post_event(std::move(ev));
        runtime.drain_events();
    }

    // Invoke a synchronous hook — same as runtime.invoke_hook, exposed here
    // for symmetry with emit().
    template<typename H>
    void invoke_hook(H& h) { runtime.invoke_hook(h); }

    // Drain and return all core domain events recorded since the last call.
    // Replaces the old core.take_core_events(): tests that were inspecting the
    // sync vector now inspect this receiver-backed list.
    std::vector<CoreDomainEvent> take_core_events() {
        runtime.drain_events();
        auto out = std::move(recorder.events);
        recorder.events.clear();
        return out;
    }

    ~TestHarness() {
        runtime.remove_receiver(&recorder);
        runtime.stop();
    }
};
