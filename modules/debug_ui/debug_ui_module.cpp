#include <module.hpp>
#include <module_registry.hpp>
#include <core.hpp>
#include <lua_host.hpp>
#include <runtime.hpp>
#include <backend/backend.hpp>
#include <backend/gl_port.hpp>
#include <log.hpp>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <GL/gl.h>

#include <chrono>
#include <deque>
#include <string>
#include <memory>
#include <unistd.h>
#include <sys/timerfd.h>

// ---------------------------------------------------------------------------
// Event log ring buffer entry.
// ---------------------------------------------------------------------------
namespace {

struct EventEntry {
    double      timestamp;  // seconds since module start
    std::string type;
    std::string detail;
};

using Clock = std::chrono::steady_clock;

// Map X11 keysyms to ImGuiKey (subset — enough for debug UI interaction).
ImGuiKey keysym_to_imgui(uint32_t ks) {
    // clang-format off
    if (ks >= 'a' && ks <= 'z') return (ImGuiKey)(ImGuiKey_A + (ks - 'a'));
    if (ks >= 'A' && ks <= 'Z') return (ImGuiKey)(ImGuiKey_A + (ks - 'A'));
    if (ks >= '0' && ks <= '9') return (ImGuiKey)(ImGuiKey_0 + (ks - '0'));
    switch (ks) {
        case 0xff09: return ImGuiKey_Tab;
        case 0xff51: return ImGuiKey_LeftArrow;
        case 0xff52: return ImGuiKey_UpArrow;
        case 0xff53: return ImGuiKey_RightArrow;
        case 0xff54: return ImGuiKey_DownArrow;
        case 0xff55: return ImGuiKey_PageUp;
        case 0xff56: return ImGuiKey_PageDown;
        case 0xff50: return ImGuiKey_Home;
        case 0xff57: return ImGuiKey_End;
        case 0xff63: return ImGuiKey_Insert;
        case 0xffff: return ImGuiKey_Delete;
        case 0xff08: return ImGuiKey_Backspace;
        case 0xff0d: return ImGuiKey_Enter;
        case 0xff1b: return ImGuiKey_Escape;
        case 0x0020: return ImGuiKey_Space;
        default:     return ImGuiKey_None;
    }
    // clang-format on
}

} // namespace

// ---------------------------------------------------------------------------
// DebugUIModule
// ---------------------------------------------------------------------------
class DebugUIModule : public Module {
    public:
        explicit DebugUIModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "debug_ui"; }

        void on_init()     override;
        void on_lua_init() override;
        void on_start()    override;
        void on_stop(bool is_exec_restart) override;

        // Event handlers — feed the event log.
        using Module::on;
        void on(event::WindowMapped ev)            override;
        void on(event::WindowUnmapped ev)          override;
        void on(event::FocusChanged ev)            override;
        void on(event::WorkspaceSwitched ev)       override;
        void on(event::DisplayTopologyChanged ev)  override;
        void on(event::KeyPressEv ev)              override;

    private:
        void toggle();
        void ensure_window();
        void destroy_window();
        void tick();
        void render_frame();

        // Panels.
        void panel_monitors();
        void panel_workspaces();
        void panel_windows();
        void panel_focus();
        void panel_events();

        void log_event(const char* type, std::string detail = {});

        void start_timer();
        void stop_timer();

        std::unique_ptr<backend::GLWindow> gl_window_;
        bool imgui_initialized_ = false;
        bool pending_close_     = false;
        int  timer_fd_          = -1;
        Clock::time_point start_time_;
        float last_frame_time_ = 0.0f;

        std::deque<EventEntry>  event_log_;
        static constexpr size_t kMaxEvents = 300;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void DebugUIModule::on_init() {
    start_time_ = Clock::now();
}

// Static pointer for Lua callbacks (only one debug_ui module instance exists).
static DebugUIModule* g_instance = nullptr;

void DebugUIModule::on_lua_init() {
    g_instance = this;

    auto& lua = this->lua();
    auto  ctx = lua.context();

    ctx.new_table();

    lua.push_callback([](LuaContext&, void*) -> int {
            if (g_instance) g_instance->toggle();
            return 0;
        }, nullptr);
    ctx.set_field(-2, "toggle");

    lua.push_callback([](LuaContext&, void*) -> int {
            if (g_instance) {
                g_instance->ensure_window();
                if (g_instance->gl_window_) {
                    g_instance->gl_window_->show();
                    g_instance->start_timer();
                }
            }
            return 0;
        }, nullptr);
    ctx.set_field(-2, "show");

    lua.push_callback([](LuaContext&, void*) -> int {
            if (g_instance && g_instance->gl_window_ && g_instance->gl_window_->visible()) {
                g_instance->gl_window_->hide();
                g_instance->stop_timer();
            }
            return 0;
        }, nullptr);
    ctx.set_field(-2, "hide");

    lua.set_module_table("debug_ui");
}

void DebugUIModule::on_start() {
    // Nothing — window is created lazily on first toggle.
}

void DebugUIModule::on_stop(bool /*is_exec_restart*/) {
    destroy_window();
}

// ---------------------------------------------------------------------------
// Window management
// ---------------------------------------------------------------------------

void DebugUIModule::toggle() {
    if (gl_window_ && gl_window_->visible()) {
        gl_window_->hide();
        stop_timer();
        return;
    }
    ensure_window();
    if (gl_window_) {
        gl_window_->show();
        start_timer();
    }
}

void DebugUIModule::ensure_window() {
    if (gl_window_)
        return;

    auto* port = runtime().ports().gl;
    if (!port) {
        LOG_WARN("debug_ui: backend does not provide GLPort — cannot create debug window");
        return;
    }

    try {
        backend::GLWindowCreateInfo info;
        info.size              = { 900, 700 };
        info.override_redirect = false;
        info.keep_above        = true;
        gl_window_             = port->create_window(info);
    } catch (const std::exception& e) {
        LOG_ERR("debug_ui: failed to create GL window: %s", e.what());
        return;
    }

    // X11 fd is NOT registered in select() — rendering and input
    // are driven entirely by the periodic timer to avoid busy-loops
    // (swap_buffers/XFlush keep the fd perpetually readable).

    // GL context must be current before initializing ImGui backend.
    gl_window_->make_current();

    // Init ImGui.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename  = nullptr; // no imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Scale UI for readability.
    ImGui::GetStyle().ScaleAllSizes(1.2f);
    io.FontGlobalScale = 1.2f;

    ImGui_ImplOpenGL3_Init("#version 130");
    imgui_initialized_ = true;

    LOG_INFO("debug_ui: window created");
}

void DebugUIModule::destroy_window() {
    stop_timer();
    if (imgui_initialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        imgui_initialized_ = false;
    }
    gl_window_.reset();
}

// ---------------------------------------------------------------------------
// Frame loop (called from watch_fd callback)
// ---------------------------------------------------------------------------

void DebugUIModule::tick() {
    if (!gl_window_)
        return;

    auto events = gl_window_->poll_events();

    // Feed events into ImGui.
    ImGuiIO& io = ImGui::GetIO();
    for (auto& ev : events) {
        switch (ev.type) {
            case backend::GLInputEvent::MouseMove:
                io.AddMousePosEvent((float)ev.pos.x(), (float)ev.pos.y());
                break;
            case backend::GLInputEvent::MouseButton:
                io.AddMouseButtonEvent(ev.button, ev.pressed);
                break;
            case backend::GLInputEvent::Scroll:
                io.AddMouseWheelEvent(0.0f, ev.scroll);
                break;
            case backend::GLInputEvent::Key: {
                ImGuiKey k = keysym_to_imgui(ev.key);
                if (k != ImGuiKey_None)
                    io.AddKeyEvent(k, ev.pressed);
                break;
            }
            case backend::GLInputEvent::Char:
                io.AddInputCharacter(ev.ch);
                break;
            case backend::GLInputEvent::Resize:
                break;
            case backend::GLInputEvent::Close:
                pending_close_ = true;
                break;
        }
    }

    if (pending_close_) {
        pending_close_ = false;
        gl_window_->hide();
        stop_timer();
        return;
    }

    if (gl_window_->visible())
        render_frame();
}

void DebugUIModule::render_frame() {
    int w = gl_window_->width();
    int h = gl_window_->height();
    if (w <= 0 || h <= 0)
        return;

    gl_window_->make_current();

    // Delta time.
    auto  now = Clock::now();
    float dt  = std::chrono::duration<float>(now - start_time_).count() - last_frame_time_;
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    last_frame_time_ = std::chrono::duration<float>(now - start_time_).count();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DeltaTime   = dt;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    // Main debug window fills the GL window.
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));
    ImGui::Begin("SirenWM Debug", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("DebugTabs")) {
        if (ImGui::BeginTabItem("Monitors")) {
            panel_monitors();   ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Workspaces")) {
            panel_workspaces(); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Windows")) {
            panel_windows();    ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Focus")) {
            panel_focus();      ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Events")) {
            panel_events();     ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    gl_window_->swap_buffers();
}

void DebugUIModule::start_timer() {
    if (timer_fd_ >= 0)
        return;
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd_ < 0)
        return;
    // 20 Hz — smooth enough for interaction; ~5% CPU at ~2.5ms/frame.
    struct itimerspec ts = {};
    ts.it_interval = { 0, 50'000'000 };  // 50ms
    ts.it_value    = { 0, 50'000'000 };
    timerfd_settime(timer_fd_, 0, &ts, nullptr);
    runtime().watch_fd(timer_fd_, [this]() {
            uint64_t expirations = 0;
            (void)read(timer_fd_, &expirations, sizeof(expirations));
            tick();
        });
}

void DebugUIModule::stop_timer() {
    if (timer_fd_ >= 0) {
        runtime().unwatch_fd(timer_fd_);
        close(timer_fd_);
        timer_fd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------

void DebugUIModule::panel_monitors() {
    const auto& mons        = core().monitor_states();
    int         focused_mon = core().focused_monitor_index();

    ImGui::Text("Monitors: %d  |  Focused: %d", (int)mons.size(), focused_mon);
    ImGui::Separator();

    if (ImGui::BeginTable("monitors", 6,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Geometry");
        ImGui::TableSetupColumn("Active WS");
        ImGui::TableSetupColumn("Focused");
        ImGui::TableSetupColumn("WS Count");
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)mons.size(); ++i) {
            const auto& m = mons[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", m.id);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(m.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d,%d %dx%d", m.x(), m.y(), m.width(), m.height());
            ImGui::TableNextColumn(); ImGui::Text("%d", m.active_ws);
            ImGui::TableNextColumn();
            if (i == focused_mon)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "YES");
            else
                ImGui::TextDisabled("no");
            ImGui::TableNextColumn();
            ImGui::Text("%d", (int)core().monitor_workspace_ids(i).size());
        }
        ImGui::EndTable();
    }
}

void DebugUIModule::panel_workspaces() {
    const auto& wss = core().workspace_states();

    ImGui::Text("Workspaces: %d", (int)wss.size());
    ImGui::Separator();

    if (ImGui::BeginTable("workspaces", 6,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Monitor");
        ImGui::TableSetupColumn("Windows");
        ImGui::TableSetupColumn("Visible");
        ImGui::TableSetupColumn("Cursor");
        ImGui::TableHeadersRow();

        for (const auto& ws : wss) {
            bool visible = core().is_workspace_visible(ws.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", ws.id);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(ws.name.c_str());
            ImGui::TableNextColumn();
            {
                int mon = core().monitor_of_workspace(ws.id);
                if (mon >= 0)
                    ImGui::Text("%d", mon);
                else
                    ImGui::TextDisabled("—");
            }
            ImGui::TableNextColumn(); ImGui::Text("%d", (int)ws.windows.size());
            ImGui::TableNextColumn();
            if (visible)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "YES");
            else
                ImGui::TextDisabled("no");
            ImGui::TableNextColumn(); ImGui::Text("%d", ws.current);
        }
        ImGui::EndTable();
    }
}

void DebugUIModule::panel_windows() {
    auto all_ids = core().all_window_ids();

    ImGui::Text("Windows: %d", (int)all_ids.size());
    ImGui::Separator();

    if (ImGui::BeginTable("windows", 9,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Class");
        ImGui::TableSetupColumn("WS");
        ImGui::TableSetupColumn("Geometry");
        ImGui::TableSetupColumn("Visible");
        ImGui::TableSetupColumn("Float");
        ImGui::TableSetupColumn("FS");
        ImGui::TableSetupColumn("BL");
        ImGui::TableSetupColumn("Type");
        ImGui::TableHeadersRow();

        for (WindowId wid : all_ids) {
            auto ws = core().window_state_any(wid);
            if (!ws) continue;

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%x", wid);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(ws->wm_class.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", core().workspace_of_window(wid));
            ImGui::TableNextColumn(); ImGui::Text("%d,%d %ux%u", ws->x(), ws->y(), ws->width(), ws->height());
            ImGui::TableNextColumn();
            if (ws->is_visible())
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "yes");
            else
                ImGui::TextDisabled("no");
            ImGui::TableNextColumn();
            if (ws->floating) ImGui::Text("F"); else ImGui::TextDisabled(".");
            ImGui::TableNextColumn();
            if (ws->fullscreen) ImGui::Text("FS"); else ImGui::TextDisabled(".");
            ImGui::TableNextColumn();
            if (ws->borderless) ImGui::Text("BL"); else ImGui::TextDisabled(".");
            ImGui::TableNextColumn();
            {
                const char* type_str = "normal";
                switch (ws->type) {
                    case WindowType::Dialog:  type_str = "dialog";  break;
                    case WindowType::Utility: type_str = "utility"; break;
                    case WindowType::Splash:  type_str = "splash";  break;
                    case WindowType::Modal:   type_str = "modal";   break;
                    case WindowType::Dock:    type_str = "dock";    break;
                    case WindowType::Desktop: type_str = "desktop"; break;
                    default: break;
                }
                ImGui::TextUnformatted(type_str);
            }
        }
        ImGui::EndTable();
    }
}

void DebugUIModule::panel_focus() {
    const auto& fs = core().focus_state();

    ImGui::Text("Monitor: %d  |  Workspace: %d  |  Window: 0x%x",
        fs.monitor, fs.ws_id, fs.window);
    ImGui::Separator();

    auto fw = core().focused_window_state();
    if (fw) {
        ImGui::Text("Class:      %s", fw->wm_class.c_str());
        ImGui::Text("Instance:   %s", fw->wm_instance.c_str());
        ImGui::Text("Geometry:   %d,%d %ux%u  border=%u",
            fw->x(), fw->y(), fw->width(), fw->height(), fw->border_width);
        ImGui::Text("Floating:   %s", fw->floating ? "yes" : "no");
        ImGui::Text("Fullscreen: %s", fw->fullscreen ? "yes" : "no");
        ImGui::Text("Borderless: %s", fw->borderless ? "yes" : "no");
        ImGui::Text("Mapped:     %s", fw->mapped ? "yes" : "no");
        ImGui::Text("Hidden WS:  %s", fw->hidden_by_workspace ? "yes" : "no");
        ImGui::Text("Hidden Exp: %s", fw->hidden_explicitly ? "yes" : "no");

        const char* type_str = "normal";
        switch (fw->type) {
            case WindowType::Dialog:  type_str = "dialog";  break;
            case WindowType::Utility: type_str = "utility"; break;
            case WindowType::Splash:  type_str = "splash";  break;
            case WindowType::Modal:   type_str = "modal";   break;
            case WindowType::Dock:    type_str = "dock";    break;
            case WindowType::Desktop: type_str = "desktop"; break;
            default: break;
        }
        ImGui::Text("Type:       %s", type_str);
    } else {
        ImGui::TextDisabled("No focused window");
    }
}

void DebugUIModule::panel_events() {
    ImGui::Text("Events: %d / %d", (int)event_log_.size(), (int)kMaxEvents);

    if (ImGui::Button("Clear"))
        event_log_.clear();

    ImGui::Separator();

    if (ImGui::BeginTable("events", 3,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Detail");
        ImGui::TableHeadersRow();

        // Show newest first.
        for (auto it = event_log_.rbegin(); it != event_log_.rend(); ++it) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%.2f", it->timestamp);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(it->type.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(it->detail.c_str());
        }

        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------
// Event log
// ---------------------------------------------------------------------------

void DebugUIModule::log_event(const char* type, std::string detail) {
    double ts = std::chrono::duration<double>(Clock::now() - start_time_).count();
    event_log_.push_back({ ts, type, std::move(detail) });
    if (event_log_.size() > kMaxEvents)
        event_log_.pop_front();
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void DebugUIModule::on(event::WindowMapped ev) {
    log_event("WindowMapped", "win=0x" + std::to_string(ev.window));
}

void DebugUIModule::on(event::WindowUnmapped ev) {
    log_event("WindowUnmapped", "win=0x" + std::to_string(ev.window));
}

void DebugUIModule::on(event::FocusChanged ev) {
    log_event("FocusChanged", "win=0x" + std::to_string(ev.window));
}

void DebugUIModule::on(event::WorkspaceSwitched ev) {
    log_event("WorkspaceSwitched", "ws=" + std::to_string(ev.workspace_id));
}

void DebugUIModule::on(event::DisplayTopologyChanged ev) {
    (void)ev;
    log_event("DisplayTopologyChanged");
}

void DebugUIModule::on(event::KeyPressEv ev) {
    log_event("KeyPress",
        "key=" + std::to_string(ev.keysym)
        + " mod=0x" + std::to_string(ev.mods));
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

SIRENWM_REGISTER_MODULE("debug_ui", DebugUIModule)
