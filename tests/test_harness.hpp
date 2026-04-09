#pragma once
// Test harness — assembles Runtime + FakeBackend
// without touching X11 or reading files from disk.
//
// Usage:
//   TestHarness h;                          // single 1920x1080 monitor
//   TestHarness h({ make_monitor(...) });   // custom topology
//   h.use<RulesModule>();                   // load specific modules
//   h.start();                              // call on_start on all modules
//   h.core.dispatch(command::MapWindow{…}); // drive the state machine

#include <core.hpp>
#include <module_registry.hpp>
#include <runtime.hpp>

#include "fake_backend.hpp"

struct TestHarness {
    ModuleRegistry module_registry;
    Runtime        runtime;
    FakeBackend    backend;

    // Shorthand so test code can say h.core.dispatch(...).
    Core& core;

    explicit TestHarness(std::vector<Monitor> monitors = {})
        : runtime(module_registry)
          , backend(monitors.empty()
            ? std::vector<Monitor>{ make_monitor(0, 0, 0, 1920, 1080, "primary") }
            : std::move(monitors))
          , core(runtime.core())
    {
        runtime.lua().init();
        runtime.bind_backend(backend);
        // Init core with fake monitors so tests can dispatch commands
        // before calling start().
        auto mons = backend.fake_monitors().get_monitors();
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
        runtime.bind_backend(backend);
        runtime.mark_configured();  // bypass load_config() for test harness
        runtime.start();
    }

    // Simulate a window being mapped: create it in core and return its id.
    WindowId map_window(WindowId id, int ws = 0) {
        core.dispatch(command::EnsureWindow{ id, ws });
        core.dispatch(command::SetWindowMapped{ id, true });
        return id;
    }

    // Emit an event through all modules (same as runtime.emit()).
    template<typename Ev>
    void emit(Ev ev) { runtime.emit(ev); }

    ~TestHarness() {
        runtime.stop();
    }
};
