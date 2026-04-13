#pragma once

#include <vector>
#include <backend/input_port.hpp>
#include <backend/keyboard_port.hpp>
#include <backend/monitor_port.hpp>
#include <backend/render_port.hpp>

class WlBackend;

namespace backend {

struct KeyIntercept {
    uint32_t keysym;
    uint32_t mods;
};

class WlInputPort final : public InputPort {
public:
    explicit WlInputPort(WlBackend& backend) : backend_(backend) {}

    void grab_key(uint32_t keysym, uint16_t mods) override;
    void ungrab_all_keys() override;
    void grab_button(WindowId window, uint8_t button, uint16_t mods) override;
    void ungrab_all_buttons(WindowId window) override;
    void grab_button_any(WindowId window) override;
    void grab_pointer() override;
    void ungrab_pointer() override;
    void allow_events(bool replay) override;
    void warp_pointer(WindowId window, Vec2i16 pos) override;
    void warp_pointer_abs(Vec2i16 pos) override;
    void flush() override;

private:
    WlBackend& backend_;
    std::vector<KeyIntercept> intercepts_;

    void send_intercepts();
};

class WlMonitorPort final : public MonitorPort {
public:
    explicit WlMonitorPort(WlBackend& backend) : backend_(backend) {}

    std::vector<Monitor> get_monitors() override;
    bool apply_monitor_layout(const std::vector<MonitorLayout>& layout) override;
    void select_change_events() override;
    void flush() override;

private:
    WlBackend& backend_;
};

class WlRenderWindow final : public RenderWindow {
public:
    WlRenderWindow(const RenderWindowCreateInfo& info, WindowId id, WlBackend& backend);
    ~WlRenderWindow() override;

    WindowId id() const override { return id_; }
    int monitor_index() const override { return monitor_index_; }
    int x() const override { return x_; }
    int y() const override { return y_; }
    int width() const override { return w_; }
    int height() const override { return h_; }

    cairo_t* cairo() override { return cr_; }
    void present() override;
    void set_visible(bool visible) override;
    void raise() override;
    void lower() override;

    uint32_t overlay_id() const { return overlay_id_; }

private:
    WlBackend&       backend_;
    WindowId         id_;
    uint32_t         overlay_id_;
    int              monitor_index_;
    int              x_, y_, w_, h_;
    cairo_surface_t* surface_ = nullptr;
    cairo_t*         cr_      = nullptr;
    bool             created_ = false;
};

class WlRenderPort final : public RenderPort {
public:
    explicit WlRenderPort(WlBackend& backend) : backend_(backend) {}

    std::unique_ptr<RenderWindow> create_window(const RenderWindowCreateInfo& info) override;
    uint32_t black_pixel() const override { return 0; }

private:
    WlBackend& backend_;
    uint32_t   next_id_ = 0x80000000;
};

class WlKeyboardPort final : public KeyboardPort {
public:
    std::string current_layout() const override;
    std::vector<std::string> layout_names() const override;
    void apply(const std::vector<std::string>& layouts, const std::string& options) override;
    void restore() override;
    uint32_t get_group() const override;
    void set_group(uint32_t group) override;

private:
    std::vector<std::string> layouts_ = {"us"};
    uint32_t group_ = 0;
};

} // namespace backend
