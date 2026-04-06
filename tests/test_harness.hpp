#pragma once
// Test harness — assembles Core + Runtime + Config + FakeBackend
// without touching X11 or reading files from disk.
//
// Usage:
//   TestHarness h;                          // single 1920x1080 monitor
//   TestHarness h({ make_monitor(...) });   // custom topology
//   h.use<RulesModule>();                   // load specific modules
//   h.start();                              // call on_start on all modules
//   h.core.dispatch(command::MapWindow{…}); // drive the state machine

#include <config.hpp>
#include <core.hpp>
#include <module_registry.hpp>
#include <runtime.hpp>

#include "fake_backend.hpp"

struct TestHarness {
    Config          config;
    ModuleRegistry  module_registry;
    Runtime         runtime;
    Core            core;
    FakeBackend     backend;

    explicit TestHarness(std::vector<Monitor> monitors = {})
        : runtime(config, module_registry)
        , backend(monitors.empty()
            ? std::vector<Monitor>{ make_monitor(0, 0, 0, 1920, 1080, "primary") }
            : std::move(monitors))
    {
        runtime.bind_backend(backend);
        auto mons = backend.fake_monitors().get_monitors();
        core.init(std::move(mons));
    }

    // Register a module (same as runtime.use<T>()).
    template<typename T, typename... Args>
    TestHarness& use(Args&&... args) {
        runtime.use<T>(core, std::forward<Args>(args)...);
        return *this;
    }

    // Call on_start on all registered modules and apply minimal CoreSettings.
    void start() {
        CoreSettings s;
        s.workspace_defs = {{ "[1]", "" }, { "[2]", "" }, { "[3]", "" }};
        core.apply_settings(s);
        runtime.start(core, backend);
    }

    // Simulate a window being mapped: create it in core and return its id.
    WindowId map_window(WindowId id, int ws = 0) {
        core.dispatch(command::EnsureWindow{ id, ws });
        core.dispatch(command::SetWindowMapped{ id, true });
        return id;
    }

    // Emit an event through all modules (same as runtime.emit()).
    template<typename Ev>
    void emit(Ev ev) { runtime.emit(core, ev); }

    ~TestHarness() {
        runtime.stop(core);
    }
};
