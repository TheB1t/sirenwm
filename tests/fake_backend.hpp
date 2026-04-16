#pragma once
// Fake backend for unit tests — no X11, no I/O.
// Provides controllable monitors, records backend effects, stubs all ports.

#include <backend/backend.hpp>
#include <backend/input_port.hpp>
#include <backend/keyboard_port.hpp>
#include <backend/monitor_port.hpp>
#include <backend/render_port.hpp>
#include <domain/monitor.hpp>

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// FakeInputPort — records grabs/warps, does nothing
// ---------------------------------------------------------------------------

class FakeInputPort : public backend::InputPort {
    public:
        struct Record {
            enum Kind { GrabKey, UngrabAll, WarpAbs, FocusWindow } kind;
            WindowId window = NO_WINDOW;
            int16_t  x = 0, y = 0;
        };
        std::vector<Record> log;

        void grab_key(uint32_t, uint16_t) override {}
        void ungrab_all_keys() override { log.push_back({ Record::UngrabAll }); }
        void grab_button(WindowId, uint8_t, uint16_t) override {}
        void ungrab_all_buttons(WindowId) override {}
        void grab_pointer() override {}
        void ungrab_pointer() override {}
        void warp_pointer(WindowId w, Vec2i16 pos) override {
            log.push_back({ Record::WarpAbs, w, pos.x(), pos.y() });
        }
        void warp_pointer_abs(Vec2i16 pos) override {
            log.push_back({ Record::WarpAbs, NO_WINDOW, pos.x(), pos.y() });
        }
        void flush() override {}
};

// ---------------------------------------------------------------------------
// FakeKeyboardPort — returns controllable layout state
// ---------------------------------------------------------------------------

class FakeKeyboardPort : public backend::KeyboardPort {
    public:
        std::string active_layout        = "us";
        std::vector<std::string> layouts = { "us" };
        bool apply_called                = false;
        bool restore_called              = false;

        std::string              current_layout() const override { return active_layout; }
        std::vector<std::string> layout_names()   const override { return layouts; }
        void apply(const std::vector<std::string>& l, const std::string&) override {
            layouts      = l;
            apply_called = true;
        }
        void restore() override { restore_called = true; }
        uint32_t get_group() const override { return group_; }
        void set_group(uint32_t g) override { group_ = g; }

    private:
        uint32_t group_ = 0;
};

// ---------------------------------------------------------------------------
// FakeMonitorPort — returns a preset monitor list, records layout applies
// ---------------------------------------------------------------------------

class FakeMonitorPort : public backend::MonitorPort {
    public:
        std::vector<Monitor> monitors;

        explicit FakeMonitorPort(std::vector<Monitor> mons = {}) : monitors(std::move(mons)) {}

        std::vector<Monitor> get_monitors() override { return monitors; }
        bool apply_monitor_layout(const std::vector<backend::MonitorLayout>&) override { return true; }
        void select_change_events() override {}
        void flush() override {}
};

// ---------------------------------------------------------------------------
// FakeRenderWindow — no-op surface
// ---------------------------------------------------------------------------

class FakeRenderWindow : public backend::RenderWindow {
    WindowId id_;
    public:
        explicit FakeRenderWindow(WindowId id) : id_(id) {}
        WindowId id() const override { return id_; }
        MonitorId monitor_index() const override { return MonitorId{ 0 }; }
        int x() const override { return 0; }
        int y() const override { return 0; }
        int width() const override { return 1920; }
        int height() const override { return 18; }
        cairo_t* cairo() override { return nullptr; }
        void present() override {}
        void set_visible(bool) override {}
        void raise() override {}
        void lower() override {}
};

// ---------------------------------------------------------------------------
// FakeRenderPort — creates FakeRenderWindows
// ---------------------------------------------------------------------------

class FakeRenderPort : public backend::RenderPort {
    uint32_t next_id_ = 0x1000;
    public:
        std::unique_ptr<backend::RenderWindow>
        create_window(const backend::RenderWindowCreateInfo&) override {
            return std::make_unique<FakeRenderWindow>(WindowId{next_id_++});
        }
        uint32_t black_pixel() const override { return 0; }
};

// ---------------------------------------------------------------------------
// FakeBackend — wires everything together
// ---------------------------------------------------------------------------

class FakeBackend : public Backend {
    FakeInputPort    input_port_;
    FakeKeyboardPort keyboard_port_;
    FakeMonitorPort  monitor_port_;
    FakeRenderPort   render_port_;
    int pipe_fds_[2] = { -1, -1 };

    public:
        FakeInputPort&    input_port    = input_port_;
        FakeKeyboardPort& keyboard_port = keyboard_port_;
        FakeMonitorPort&  monitor_port  = monitor_port_;

        explicit FakeBackend(std::vector<Monitor> monitors = {})
            : monitor_port_(std::move(monitors)) {
            pipe(pipe_fds_); // dummy event fd
        }
        FakeBackend(Core&, Runtime&, std::vector<Monitor> monitors = {})
            : FakeBackend(std::move(monitors)) {}
        ~FakeBackend() override {
            if (pipe_fds_[0] >= 0) close(pipe_fds_[0]);
            if (pipe_fds_[1] >= 0) close(pipe_fds_[1]);
        }

        int  event_fd() const override { return pipe_fds_[0]; }
        void pump_events(std::size_t) override {}
        void render_frame() override {}
        void on_reload_applied() override {}

        backend::BackendPorts ports() override {
            return backend::BackendPorts{
                . input   = input_port_,
                .monitor  = monitor_port_,
                .render   = render_port_,
                .keyboard = keyboard_port_,
            };
        }

        // Convenience: inject a monitor topology change
        void set_monitors(std::vector<Monitor> mons) {
            monitor_port_.monitors = std::move(mons);
        }
};

// ---------------------------------------------------------------------------
// Helper: build a simple Monitor for tests
// ---------------------------------------------------------------------------

inline Monitor make_monitor(int id, int x, int y, int w, int h,
    const std::string& name = "") {
    return Monitor(id, name.empty() ? ("mon" + std::to_string(id)) : name, x, y, w, h);
}
